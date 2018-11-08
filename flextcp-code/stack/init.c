#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <flexnic_driver.h>
#include <kernel_appif.h>
#include <flextcp.h>
#include <flextcp_plif.h>
#include <utils_timeout.h>
#include "internal.h"

static inline int event_kappin_conn_opened(
    struct kernel_appin_conn_opened *inev, struct flextcp_event *outev,
    unsigned avail);
static inline void event_kappin_listen_newconn(
    struct kernel_appin_listen_newconn *inev, struct flextcp_event *outev);
static inline int event_kappin_accept_conn(
    struct kernel_appin_accept_conn *inev, struct flextcp_event *outev,
    unsigned avail);
static inline void event_kappin_st_conn_move(
    struct kernel_appin_status *inev, struct flextcp_event *outev);
static inline void event_kappin_st_listen_open(
    struct kernel_appin_status *inev, struct flextcp_event *outev);

static inline int event_arx_connupdate(struct flextcp_context *ctx,
    struct flextcp_pl_arx_connupdate *inev,
    struct flextcp_event *outevs, int outn, uint16_t fn_core);
static inline int event_arx_objupdate(struct flextcp_context *ctx,
    struct flextcp_pl_arx_connupdate *inev, struct flextcp_event *outevs,
    int outn);

static inline void conns_bump(struct flextcp_context *ctx);

void *flexnic_mem = NULL;
static struct flexnic_info *flexnic_info = NULL;
int flexnic_evfd[FLEXTCP_MAX_FTCPCORES];

void flextcp_block(struct flextcp_context *ctx, int timeout_ms)
{
  assert(ctx->evfd != 0);
  /* fprintf(stderr, "[%d] idle - timeout %d ms\n", ctx->ctx_id, timeout_ms); */
  struct epoll_event event[1];
  int n;
again:
  n = epoll_wait(ctx->epfd, event, 1, timeout_ms);
  if(n == -1) {
    if(errno == EINTR) {
      // XXX: To support attaching GDB
      goto again;
    }
    fprintf(stderr, "[%d] errno = %d\n", ctx->ctx_id, errno);
  }
  assert(n != -1);
  /* fprintf(stderr, "[%d] busy - %u events, kout head = %u\n", ctx->ctx_id, n, ctx->kout_head); */
  for(int i = 0; i < n; i++) {
    assert(event[i].data.fd == ctx->evfd);

    uint64_t val;
    /* fprintf(stderr, "[%d] - woken up by event FD = %d\n", ctx->ctx_id, event[i].data.fd); */
    int r = read(ctx->evfd, &val, sizeof(uint64_t));
    assert(r == sizeof(uint64_t));
  }
}

int flextcp_init(void)
{
  if (flextcp_kernel_connect() != 0) {
    fprintf(stderr, "flextcp_init: connecting to kernel failed\n");
    return -1;
  }

  if (flexnic_driver_connect(&flexnic_info, &flexnic_mem) != 0) {
    fprintf(stderr, "flextcp_init: connecting to flexnic failed\n");
    return -1;
  }

  return 0;
}

int flextcp_context_create(struct flextcp_context *ctx)
{
  static uint16_t ctx_id = 0;

  ctx->ctx_id = __sync_fetch_and_add(&ctx_id, 1);
  if (ctx->ctx_id >= FLEXTCP_MAX_CONTEXTS) {
    fprintf(stderr, "flextcp_context_create: maximum number of contexts "
        "exeeded\n");
    return -1;
  }

  ctx->evfd = eventfd(0, 0);
  assert(ctx->evfd != -1);

  struct epoll_event ev = {
    .events = EPOLLIN,
    .data.fd = ctx->evfd,
  };

  ctx->epfd = epoll_create1(0);
  assert(ctx->epfd != -1);

  int r = epoll_ctl(ctx->epfd, EPOLL_CTL_ADD, ctx->evfd, &ev);
  assert(r == 0);

  return flextcp_kernel_newctx(ctx);
}

#include <pthread.h>

int debug_flextcp_on = 0;

int flextcp_context_poll(struct flextcp_context *ctx, int num,
    struct flextcp_event *events)
{
  int i, j = 0, ran_out;
  struct kernel_appin *kout;
  struct flextcp_pl_arx *arx_q, *arx;
  uint32_t pos, head;
  uint16_t k;

  /* poll kernel queues */
  pos = ctx->kout_head;
  for (i = 0; i < num;) {
    kout = (struct kernel_appin *) ctx->kout_base + pos;
    j = 1;
    if (kout->type == KERNEL_APPIN_INVALID) {
      break;
    } else if (kout->type == KERNEL_APPIN_CONN_OPENED) {
      j = event_kappin_conn_opened(&kout->data.conn_opened, &events[i],
          num - i);
    } else if (kout->type == KERNEL_APPIN_LISTEN_NEWCONN) {
      event_kappin_listen_newconn(&kout->data.listen_newconn, &events[i]);
    } else if (kout->type == KERNEL_APPIN_ACCEPTED_CONN) {
      j = event_kappin_accept_conn(&kout->data.accept_connection, &events[i],
          num - i);
    } else if (kout->type == KERNEL_APPIN_STATUS_LISTEN_OPEN) {
      event_kappin_st_listen_open(&kout->data.status, &events[i]);
    } else if (kout->type == KERNEL_APPIN_STATUS_CONN_MOVE) {
      event_kappin_st_conn_move(&kout->data.status, &events[i]);
    } else {
      fprintf(stderr, "flextcp_context_poll: unexpected kout type=%u pos=%u len=%u\n",
          kout->type, pos, ctx->kout_len);
      abort();
    }

    if (j == -1) {
      break;
    }

    i += j;
    kout->type = KERNEL_APPIN_INVALID;

    pos = pos + 1;
    if (pos >= ctx->kout_len) {
      pos = 0;
    }
  }
  ctx->kout_head = pos;

  /* stop if we didn't have enough queue space */
  if (j == -1) {
    return i;
  }

  /* poll NIC queues */
  for (k = 0; k < ctx->num_queues && i < num; k++) {
    ran_out = 0;

    arx_q = (struct flextcp_pl_arx *) ctx->queues[ctx->next_queue].rxq_base;
    head = ctx->queues[ctx->next_queue].rxq_head;
    for (; i < num;) {
      j = 0;
      arx = &arx_q[head / sizeof(*arx)];
      if (arx->type == FLEXTCP_PL_ARX_INVALID) {
        break;
      } else if (arx->type == FLEXTCP_PL_ARX_DUMMY) {
        /* no need to do anything, just update tx head */
      } else if (arx->type == FLEXTCP_PL_ARX_CONNUPDATE) {
        j = event_arx_connupdate(ctx, &arx->msg.connupdate, events + i, num - i, ctx->next_queue);
      } else if (arx->type == FLEXTCP_PL_ARX_OBJUPDATE) {
        j = event_arx_objupdate(ctx, &arx->msg.connupdate, events + i, num - i);
      } else {
        fprintf(stderr, "flextcp_context_poll: kout type=%u head=%x\n", arx->type, head);
      }

      if (j == -1) {
        ran_out = 1;
        break;
      }
      i += j;

      arx->type = 0;

      /* next entry */
      head += sizeof(*arx);
      if (head >= ctx->rxq_len) {
          head -= ctx->rxq_len;
      }
    }

    ctx->queues[ctx->next_queue].rxq_head = head;
    if (ran_out)
      break;

    ctx->next_queue = ctx->next_queue + 1;
    if (ctx->next_queue >= ctx->num_queues)
      ctx->next_queue -= ctx->num_queues;
  }

  conns_bump(ctx);

  /* TODO: NULL queue */

  return i;
}

int flextcp_context_tx_alloc(struct flextcp_context *ctx,
    struct flextcp_pl_atx **patx, uint16_t core)
{
  struct flextcp_pl_atx *atx = (struct flextcp_pl_atx *)
    (ctx->queues[core].txq_base + ctx->queues[core].txq_tail);

  /* if queue is full, abort */
  if (atx->type != 0) {
    return -1;
  }

  *patx = atx;
  return 0;
}

static void flextcp_flexnic_kick(int core)
{
  uint32_t now = util_timeout_time_us();
  static uint32_t last_ts[FLEXTCP_MAX_FTCPCORES];

  /* fprintf(stderr, "kicking flexnic?\n"); */

  if(now - last_ts[core] > POLL_CYCLE) {
    // Kick
    /* fprintf(stderr, "kicking flexnic on %d\n", flexnic_evfd[core]); */
    uint64_t val = 1;
    int r = write(flexnic_evfd[core], &val, sizeof(uint64_t));
    assert(r == sizeof(uint64_t));
  }

  // XXX: Unprotected write -- should be OK
  last_ts[core] = now;
}

void flextcp_context_tx_done(struct flextcp_context *ctx, uint16_t core)
{
  ctx->queues[core].txq_tail += sizeof(struct flextcp_pl_atx);
  if (ctx->queues[core].txq_tail >= ctx->txq_len) {
    ctx->queues[core].txq_tail -= ctx->txq_len;
  }

  flextcp_flexnic_kick(core);
}

static inline int event_kappin_conn_opened(
    struct kernel_appin_conn_opened *inev, struct flextcp_event *outev,
    unsigned avail)
{
  struct flextcp_obj_connection *oconn;
  struct flextcp_connection *conn;
  int j = 1;

  /* depending on whether this is an object connection, we issue different
   * events */
  if (OPAQUE_ISOBJ(inev->opaque)) {
    oconn = OPAQUE_PTR(inev->opaque);
    conn = &oconn->c;

    outev->event_type = FLEXTCP_EV_OBJ_CONN_OPEN;
    outev->ev.obj_conn_open.status = inev->status;
    outev->ev.obj_conn_open.conn = oconn;
  } else {
    conn = OPAQUE_PTR(inev->opaque);

    outev->event_type = FLEXTCP_EV_CONN_OPEN;
    outev->ev.conn_open.status = inev->status;
    outev->ev.conn_open.conn = conn;
  }

  if (inev->status != 0) {
    conn->status = CONN_CLOSED;
    return 1;
  } else if (conn->rxb_head > 0 && avail < 2) {
    /* if we've already received updates, we'll need to inject them */
    return -1;
  }

  conn->status = CONN_OPEN;
  conn->local_ip = inev->local_ip;
  conn->local_port = inev->local_port;
  conn->seq_rx = inev->seq_rx;
  conn->seq_tx = inev->seq_tx;
  conn->flow_id = inev->flow_id;
  conn->fn_core = inev->fn_core;

  conn->rxb_base = (uint8_t *) flexnic_mem + inev->rx_off;
  conn->rxb_len = inev->rx_len;

  conn->txb_base = (uint8_t *) flexnic_mem + inev->tx_off;
  conn->txb_len = inev->tx_len;

  /* inject bump if necessary */
  if (conn->rxb_head > 0) {
    fprintf(stderr, "injecting bump\n");
    assert(conn->rxb_head < conn->rxb_len);
    conn->seq_rx += conn->rxb_head;

    outev[1].event_type = FLEXTCP_EV_CONN_RECEIVED;
    outev[1].ev.conn_received.conn = conn;
    outev[1].ev.conn_received.buf = conn->rxb_base;
    outev[1].ev.conn_received.len = conn->rxb_head;
    j++;
  }

  return j;
}

static inline void event_kappin_listen_newconn(
    struct kernel_appin_listen_newconn *inev, struct flextcp_event *outev)
{
  struct flextcp_obj_listener *olistener;
  struct flextcp_listener *listener;

  /* depending on whether this is an object listener, we issue different
   * events */
  if (OPAQUE_ISOBJ(inev->opaque)) {
    olistener = OPAQUE_PTR(inev->opaque);

    outev->event_type = FLEXTCP_EV_OBJ_LISTEN_NEWCONN;
    outev->ev.obj_listen_newconn.remote_ip = inev->remote_ip;
    outev->ev.obj_listen_newconn.remote_port = inev->remote_port;
    outev->ev.obj_listen_open.listener = olistener;
  } else {
    listener = OPAQUE_PTR(inev->opaque);

    outev->event_type = FLEXTCP_EV_LISTEN_NEWCONN;
    outev->ev.listen_newconn.remote_ip = inev->remote_ip;
    outev->ev.listen_newconn.remote_port = inev->remote_port;
    outev->ev.listen_open.listener = listener;
  }
}

static inline int event_kappin_accept_conn(
    struct kernel_appin_accept_conn *inev, struct flextcp_event *outev,
    unsigned avail)
{
  struct flextcp_obj_connection *oconn;
  struct flextcp_connection *conn;
  int j = 1;

  /* depending on whether this is an object connection, we issue different
   * events */
  if (OPAQUE_ISOBJ(inev->opaque)) {
    oconn = OPAQUE_PTR(inev->opaque);
    conn = &oconn->c;

    outev->event_type = FLEXTCP_EV_OBJ_LISTEN_ACCEPT;
    outev->ev.obj_listen_accept.status = inev->status;
    outev->ev.obj_listen_accept.conn = oconn;
  } else {
    conn = OPAQUE_PTR(inev->opaque);

    outev->event_type = FLEXTCP_EV_LISTEN_ACCEPT;
    outev->ev.listen_accept.status = inev->status;
    outev->ev.listen_accept.conn = conn;
  }

  if (inev->status != 0) {
    conn->status = CONN_CLOSED;
    return 1;
  } else if (conn->rxb_head > 0 && avail < 2) {
    /* if we've already received updates, we'll need to inject them */
    return -1;
  }

  conn->status = CONN_OPEN;
  conn->local_ip = inev->local_ip;
  conn->remote_ip = inev->remote_ip;
  conn->remote_port = inev->remote_port;
  conn->seq_rx = inev->seq_rx;
  conn->seq_tx = inev->seq_tx;
  conn->flow_id = inev->flow_id;
  conn->fn_core = inev->fn_core;

  conn->rxb_base = (uint8_t *) flexnic_mem + inev->rx_off;
  conn->rxb_len = inev->rx_len;

  conn->txb_base = (uint8_t *) flexnic_mem + inev->tx_off;
  conn->txb_len = inev->tx_len;

  /* inject bump if necessary */
  if (conn->rxb_head > 0) {
    fprintf(stderr, "injecting bump\n");
    assert(conn->rxb_head < conn->rxb_len);
    conn->seq_rx += conn->rxb_head;

    outev[1].event_type = FLEXTCP_EV_CONN_RECEIVED;
    outev[1].ev.conn_received.conn = conn;
    outev[1].ev.conn_received.buf = conn->rxb_base;
    outev[1].ev.conn_received.len = conn->rxb_head;
    j++;
  }

  return j;
}

static inline void event_kappin_st_conn_move(
    struct kernel_appin_status *inev, struct flextcp_event *outev)
{
  struct flextcp_connection *conn;

  conn = OPAQUE_PTR(inev->opaque);

  outev->event_type = FLEXTCP_EV_CONN_MOVED;
  outev->ev.conn_moved.status = inev->status;
  outev->ev.conn_moved.conn = conn;
}

static inline void event_kappin_st_listen_open(
    struct kernel_appin_status *inev, struct flextcp_event *outev)
{
  struct flextcp_obj_listener *olistener;
  struct flextcp_listener *listener;

  /* depending on whether this is an object listener, we issue different
   * events */
  if (OPAQUE_ISOBJ(inev->opaque)) {
    olistener = OPAQUE_PTR(inev->opaque);

    outev->event_type = FLEXTCP_EV_OBJ_LISTEN_OPEN;
    outev->ev.obj_listen_open.status = inev->status;
    outev->ev.obj_listen_open.listener = olistener;
  } else {
    listener = OPAQUE_PTR(inev->opaque);

    outev->event_type = FLEXTCP_EV_LISTEN_OPEN;
    outev->ev.listen_open.status = inev->status;
    outev->ev.listen_open.listener = listener;
  }
}

static inline int event_arx_connupdate(struct flextcp_context *ctx,
    struct flextcp_pl_arx_connupdate *inev, struct flextcp_event *outevs,
    int outn, uint16_t fn_core)
{
  struct flextcp_connection *conn;
  uint32_t rx_bump, rx_len, tx_bump;
  int i = 0, evs_needed, tx_avail_ev;

  conn = OPAQUE_PTR(inev->opaque);

  conn->fn_core = fn_core;

  rx_bump = inev->rx_bump;
  tx_bump = inev->tx_bump;

  if (conn->status != CONN_OPEN) {
    /* due to a race we might see connection updates before we see the
     * connection confirmation from the kernel */
    assert(conn->status == CONN_OPEN_REQUESTED ||
        conn->status == CONN_ACCEPT_REQUESTED);
    assert(tx_bump == 0);
    conn->rxb_head += rx_bump;
    return 0;
  }

  /* figure out how many events for rx */
  evs_needed = 0;
  if (rx_bump > 0) {
    evs_needed++;
    if (conn->rxb_head + rx_bump > conn->rxb_len) {
      evs_needed++;
    }
  }

  /* if tx buffer was depleted, we'll generate a tx avail event */
  tx_avail_ev = (tx_bump > 0 && flextcp_conn_txbuf_available(conn) == 0);
  if (tx_avail_ev) {
    evs_needed++;
  }

  /* if we can't fit all events, try again later */
  if (evs_needed > outn) {
    return -1;
  }

  /* generate rx events */
  if (rx_bump > 0) {
    outevs[i].event_type = FLEXTCP_EV_CONN_RECEIVED;
    outevs[i].ev.conn_received.conn = conn;
    outevs[i].ev.conn_received.buf = conn->rxb_base + conn->rxb_head;
    if (conn->rxb_head + rx_bump > conn->rxb_len) {
      /* wrap around in rx buffer */
      rx_len = conn->rxb_len - conn->rxb_head;
      outevs[i].ev.conn_received.len = rx_len;

      i++;
      outevs[i].event_type = FLEXTCP_EV_CONN_RECEIVED;
      outevs[i].ev.conn_received.conn = conn;
      outevs[i].ev.conn_received.buf = conn->rxb_base;
      outevs[i].ev.conn_received.len = rx_bump - rx_len;
    } else {
      outevs[i].ev.conn_received.len = rx_bump;
    }
    i++;

    /* update rx buffer */
    conn->seq_rx += rx_bump;
    conn->rxb_head += rx_bump;
    if (conn->rxb_head >= conn->rxb_len) {
      conn->rxb_head -= conn->rxb_len;
    }
  }

  /* bump tx */
  if (tx_bump > 0) {
    conn->txb_tail += tx_bump;
    if (conn->txb_tail >= conn->txb_len) {
      conn->txb_tail -= conn->txb_len;
    }

    if (tx_avail_ev) {
      outevs[i].event_type = FLEXTCP_EV_CONN_SENDBUF;
      outevs[i].ev.conn_sendbuf.conn = conn;
      i++;
    }
  }

  return i;
}

static inline int event_arx_objupdate(struct flextcp_context *ctx,
    struct flextcp_pl_arx_connupdate *inev, struct flextcp_event *outevs,
    int outn)
{
  int i = 0, txev;
  struct flextcp_obj_connection *oc;
  struct flextcp_connection *conn;
  struct flextcp_obj_conn_ctx *cc;
  struct obj_hdr oh;
  uint32_t obj_pos, obj_len = 0, data_start, data_len, rx_bump, tx_bump,
           obj_end;
  size_t l;

  if (outn < 2) {
    return -1;
  }

  oc = OPAQUE_PTR(inev->opaque);
  conn = &oc->c;
  cc = &oc->ctx[ctx->ctx_id];

  assert(conn->status == CONN_OPEN);

  rx_bump = inev->rx_bump;
  tx_bump = inev->tx_bump;

  /* catch notifications that don't bump RX */
  if (rx_bump == 0) {
    if (tx_bump > 0) {
      oconn_lock(oc);
      txev = flextcp_conn_txbuf_available(conn) == 0;
      conn->txb_tail = circ_offset(conn->txb_tail, conn->txb_len, tx_bump);
      oconn_unlock(oc);

      if (txev) {
        outevs[i].event_type = FLEXTCP_EV_CONN_SENDBUF;
        outevs[i].ev.conn_sendbuf.conn = conn;
        i++;
      }
    }
    return i;
  }

  /* Now we know that there is definitely an RX bump */

  /* Check if this is a new object */
  obj_len = cc->obj_len_rem;
  obj_pos = cc->obj_pos;
  if (obj_len == 0) {
    if (inev->rx_bump < sizeof(oh)) {
      fprintf(stderr, "event_arx_objupdate: rx bump for new object smaller"
          " than object header (got %u expect %zu)\n", inev->rx_bump,
          sizeof(oh));
      abort();
    }

    /* get object header */
    circ_read(&oh, conn->rxb_base, conn->rxb_len, inev->rx_pos, sizeof(oh));

    /* calculate object length */
    obj_len = sizeof(oh) + oh.dstlen + f_beui32(oh.len);
    obj_pos = inev->rx_pos;
  }

  if (rx_bump > obj_len) {
    fprintf(stderr, "event_arx_objupdate: rx bump larger than object (got %u "
        " expect %u)\n", rx_bump, obj_len);
    abort();
  }

  obj_len -= rx_bump;

  /* Check if this object is complete */
  if (obj_len == 0) {
    /* get object header */
    circ_read(&oh, conn->rxb_base, conn->rxb_len, obj_pos, sizeof(oh));

    /* calculate length and start position of data */
    data_len = oh.dstlen + f_beui32(oh.len);
    data_start = circ_offset(obj_pos, conn->rxb_len, sizeof(oh));

    /* fill in event */
    outevs[i].event_type = FLEXTCP_EV_OBJ_CONN_RECEIVED;
    outevs[i].ev.obj_conn_received.handle.pos = obj_pos;
    outevs[i].ev.obj_conn_received.conn = oc;
    outevs[i].ev.obj_conn_received.dstlen = oh.dstlen;

    circ_range(&outevs[i].ev.obj_conn_received.buf_1, &l,
          &outevs[i].ev.obj_conn_received.buf_2, conn->rxb_base, conn->rxb_len,
          data_start, data_len);
    outevs[i].ev.obj_conn_received.len_1 = l;
    outevs[i].ev.obj_conn_received.len_2 = data_len - l;
    i++;

    /* calculate object end position to update rx head */
    obj_end = circ_offset(obj_pos, conn->rxb_len, sizeof(oh) + data_len);
  }

  /* update context local state */
  cc->obj_len_rem = obj_len;
  cc->obj_pos = obj_pos;

  oconn_lock(oc);

  /* if this object is beyond any previously received objects, bump rxb_head */
  if (obj_len == 0 && circ_in_interval(conn->rxb_head, conn->rxb_tail,
        conn->rxb_len, obj_end))
  {
    conn->rxb_head = obj_end;
  }

  /* bump tx */
  if (tx_bump > 0) {
    txev = flextcp_conn_txbuf_available(conn) == 0;

    conn->txb_tail = circ_offset(conn->txb_tail, conn->txb_len, tx_bump);

    if (txev) {
      outevs[i].event_type = FLEXTCP_EV_OBJ_CONN_SENDBUF;
      outevs[i].ev.obj_conn_sendbuf.conn = oc;
      i++;
    }
  }

  oconn_unlock(oc);

  return i;
}


static inline void conns_bump(struct flextcp_context *ctx)
{
  struct flextcp_connection *c;
  struct flextcp_pl_atx *atx;

  while ((c = ctx->bump_pending_first) != NULL) {
    if (flextcp_context_tx_alloc(ctx, &atx, c->fn_core) != 0) {
      break;
    }

    atx->msg.connupdate.rx_tail = c->rxb_tail;
    atx->msg.connupdate.tx_head = c->txb_head;
    atx->msg.connupdate.flow_id = c->flow_id;
    atx->msg.connupdate.bump_seq = c->bump_seq++;
    MEM_BARRIER();
    atx->type = FLEXTCP_PL_ATX_CONNUPDATE;

    flextcp_context_tx_done(ctx, c->fn_core);

    c->rxb_nictail = c->rxb_tail;
    c->bump_pending = 0;

    if (c->bump_next == NULL) {
      ctx->bump_pending_last = NULL;
    }
    ctx->bump_pending_first = c->bump_next;
  }
}
