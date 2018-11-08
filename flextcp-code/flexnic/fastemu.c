#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <rte_config.h>
#include <rte_malloc.h>
#include <rte_cycles.h>

#include <flextcp_plif.h>

#include "internal.h"
#include "fastemu.h"

#define DATAPLANE_TSCS

#ifdef DATAPLANE_STATS
# ifdef DATAPLANE_TSCS
#   define STATS_TS(n) uint64_t n = rte_get_tsc_cycles()
#   define STATS_TSADD(c, f, n) __sync_fetch_and_add(&c->stat_##f, n)
# else
#   define STATS_TS(n) do { } while (0)
#   define STATS_TSADD(c, f, n) do { } while (0)
# endif
#   define STATS_ADD(c, f, n) __sync_fetch_and_add(&c->stat_##f, n)
#else
#   define STATS_TS(n) do { } while (0)
#   define STATS_TSADD(c, f, n) do { } while (0)
#   define STATS_ADD(c, f, n) do { } while (0)
#endif


static unsigned num_threads;
struct dataplane_context **ctxs = NULL;

static unsigned poll_rx(struct dataplane_context *ctx, uint32_t ts);
static unsigned poll_queues(struct dataplane_context *ctx, uint32_t ts);
static unsigned poll_kernel(struct dataplane_context *ctx, uint32_t ts);
static unsigned poll_qman(struct dataplane_context *ctx, uint32_t ts);
static unsigned poll_qman_fwd(struct dataplane_context *ctx, uint32_t ts);
static void poll_scale(struct dataplane_context *ctx);

static inline uint8_t bufcache_prealloc(struct dataplane_context *ctx, uint16_t num,
    struct network_buf_handle ***handles, void ***bufs);
static inline void bufcache_alloc(struct dataplane_context *ctx, uint16_t num);
static inline void bufcache_free(struct dataplane_context *ctx,
    struct network_buf_handle *handle, void *buf);

static inline void tx_flush(struct dataplane_context *ctx);
static inline void tx_send(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, uint16_t off, uint16_t len);

int dataplane_init(unsigned threads)
{
  num_threads = threads;

  if (FLEXNIC_INTERNAL_MEM_SIZE < sizeof(struct flextcp_pl_mem)) {
    fprintf(stderr, "dataplane_init: internal flexnic memory size not "
        "sufficient (got %x, need %zx)\n", FLEXNIC_INTERNAL_MEM_SIZE,
        sizeof(struct flextcp_pl_mem));
    return -1;
  }

  if (threads > FLEXNIC_PL_APPST_CTX_MCS) {
    fprintf(stderr, "dataplane_init: more cores than FLEXNIC_PL_APPST_CTX_MCS "
        "(%u)\n", FLEXNIC_PL_APPST_CTX_MCS);
    return -1;
  }

  if ((ctxs = calloc(threads, sizeof(*ctxs))) == NULL) {
    perror("datplane_init: calloc failed");
    return -1;
  }

  return 0;
}

struct dataplane_context *dataplane_context_init(uint16_t id,
    struct network_rx_thread *rx_t, struct network_tx_thread *tx_t,
    struct qman_thread *qman_t)
{
  char name[32];
  struct dataplane_context *ctx;

  sprintf(name, "qman_fwd_ring_%u", id);

  if ((ctx = rte_zmalloc("dataplane context", sizeof(*ctx), 0)) == NULL) {
    return NULL;
  }
  if ((ctx->qman_fwd_ring = rte_ring_create(name, 8 * 1024, rte_socket_id(),
          RING_F_SC_DEQ)) == NULL)
  {
    rte_free(ctx);
    return NULL;
  }


  ctx->id = id;

  ctx->rx_t = rx_t;
  ctx->tx_t = tx_t;
  ctx->qman_t = qman_t;
  ctx->poll_next_ctx = id;
  ctxs[id] = ctx;

  ctx->evfd = eventfd(0, 0);
  assert(ctx->evfd != -1);
  ctx->ev.epdata.event = EPOLLIN;
  int r = rte_epoll_ctl(RTE_EPOLL_PER_THREAD, EPOLL_CTL_ADD, ctx->evfd, &ctx->ev);
  assert(r == 0);

  pl_memory->kctx[ctx->id].evfd = ctx->evfd;
  return ctx;
}

void dataplane_context_destroy(struct dataplane_context *ctx)
{
  rte_free(ctx);
}

void dataplane_loop(struct dataplane_context *ctx)
{
  uint32_t ts, startwait = 0;
  uint64_t cyc, prev_cyc;
  int was_idle = 1;

  while (!exited) {
    unsigned n = 0;

    /* count cycles of previous iteration if it was busy */
    prev_cyc = cyc;
    cyc = rte_get_tsc_cycles();
    if (!was_idle)
      ctx->loadmon_cyc_busy += cyc - prev_cyc;


    ts = qman_timestamp(cyc);

    STATS_TS(start);
    n += poll_rx(ctx, ts);
    STATS_TS(rx);
    tx_flush(ctx);

    n += poll_qman_fwd(ctx, ts);

    STATS_TSADD(ctx, cyc_rx, rx - start);
    n += poll_qman(ctx, ts);
    STATS_TS(qm);
    STATS_TSADD(ctx, cyc_qm, qm - rx);
    n += poll_queues(ctx, ts);
    STATS_TS(qs);
    STATS_TSADD(ctx, cyc_qs, qs - qm);
    n += poll_kernel(ctx, ts);

    /* flush transmit buffer */
    tx_flush(ctx);

    if (ctx->id == 0)
      poll_scale(ctx);

    if(UNLIKELY(n == 0)) {
      was_idle = 1;

      if(startwait == 0) {
	startwait = ts;
      } else if(ts - startwait >= POLL_CYCLE) {
	// Idle -- wait for interrupt or data from apps/kernel
	int r = network_rx_interrupt_ctl(ctx->rx_t, 1);

	// Only if device running
	if(r == 0) {
	  uint32_t timeout_us = qman_next_ts(ctx->qman_t, ts);
	  /* fprintf(stderr, "[%u] fastemu idle - timeout %d ms\n", ctx->core, */
	  /* 	  timeout_us == (uint32_t)-1 ? -1 : timeout_us / 1000); */
	  struct rte_epoll_event event[2];
	  int n = rte_epoll_wait(RTE_EPOLL_PER_THREAD, event, 2,
				 timeout_us == (uint32_t)-1 ? -1 : timeout_us / 1000);
	  assert(n != -1);
	  /* fprintf(stderr, "[%u] fastemu busy - %u events\n", ctx->core, n); */
	  for(int i = 0; i < n; i++) {
	    if(event[i].fd == ctx->evfd) {
	      /* fprintf(stderr, "[%u] fastemu - woken up by event FD = %d\n", */
	      /* 	      ctx->core, event[i].fd); */
	      uint64_t val;
	      int r = read(ctx->evfd, &val, sizeof(uint64_t));
	      assert(r == sizeof(uint64_t));
	    /* } else { */
	    /*   fprintf(stderr, "[%u] fastemu - woken up by RX interrupt FD = %d\n", */
	    /* 	      ctx->core, event[i].fd); */
	    }
	  }

          /*fprintf(stderr, "dataplane_loop: woke up %u n=%u fd=%d evfd=%d\n", ctx->id, n, event[0].fd, ctx->evfd);*/
	  /* network_rx_interrupt_ctl(ctx->rx_t, 0); */
	}
      startwait = 0;
      }
    } else {
      was_idle = 0;
      startwait = 0;
    }
  }
}

#ifdef DATAPLANE_STATS
static inline uint64_t read_stat(uint64_t *p)
{
  return __sync_lock_test_and_set(p, 0);
}

void dataplane_dump_stats(void)
{
  struct dataplane_context *ctx;
  unsigned i;

  for (i = 0; i < num_threads; i++) {
    ctx = ctxs[i];
    fprintf(stderr, "dp stats %u: "
        "qm=(%"PRIu64",%"PRIu64",%"PRIu64")  "
        "rx=(%"PRIu64",%"PRIu64",%"PRIu64")  "
        "qs=(%"PRIu64",%"PRIu64",%"PRIu64")  "
        "cyc=(%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64")\n", i,
        read_stat(&ctx->stat_qm_poll), read_stat(&ctx->stat_qm_empty),
        read_stat(&ctx->stat_qm_total),
        read_stat(&ctx->stat_rx_poll), read_stat(&ctx->stat_rx_empty),
        read_stat(&ctx->stat_rx_total),
        read_stat(&ctx->stat_qs_poll), read_stat(&ctx->stat_qs_empty),
        read_stat(&ctx->stat_qs_total),
        read_stat(&ctx->stat_cyc_db), read_stat(&ctx->stat_cyc_qm),
        read_stat(&ctx->stat_cyc_rx), read_stat(&ctx->stat_cyc_qs));
  }
}
#endif

static unsigned poll_rx(struct dataplane_context *ctx, uint32_t ts)
{
  int ret;
  unsigned i, n;
  uint16_t offs[BATCH_SIZE];
  uint16_t lens[BATCH_SIZE];
  uint8_t freebuf[BATCH_SIZE] = { 0 };
  void *bufs[BATCH_SIZE];
  void *fss[BATCH_SIZE];
  struct network_buf_handle *bhs[BATCH_SIZE];

  n = BATCH_SIZE;
  if (TXBUF_SIZE - ctx->tx_num < n)
    n = TXBUF_SIZE - ctx->tx_num;

  STATS_ADD(ctx, rx_poll, 1);

  /* receive packets */
  ret = network_poll(ctx->rx_t, n, offs, lens, bufs, bhs);
  if (ret <= 0) {
    STATS_ADD(ctx, rx_empty, 1);
    return 0;
  }
  STATS_ADD(ctx, rx_total, n);
  n = ret;

  /* prefetch packet contents */
  for (i = 0; i < n; i++) {
    rte_prefetch0(bufs[i] + offs[i]);
  }

  /* look up flow states */
  fast_flows_packet_fss(ctx, bufs, offs, fss, n);

  for (i = 0; i < n; i++) {
    /* run fast-path for flows with flow state */
    if (fss[i] != NULL) {
      ret = fast_flows_packet(ctx, bufs[i], offs[i], lens[i], bhs[i], fss[i],
          ts);
    } else {
      ret = -1;
    }

    if (ret > 0) {
      freebuf[i] = 1;
    } else if (ret < 0) {
      fast_kernel_packet(ctx, bufs[i], offs[i], lens[i], bhs[i]);
    }
  }

  /* free received buffers */
  for (i = 0; i < n; i++) {
    if (freebuf[i] == 0)
      bufcache_free(ctx, bhs[i], bufs[i]);
  }

  return n;
}

static unsigned poll_queues(struct dataplane_context *ctx, uint32_t ts)
{
  struct network_buf_handle **handles;
  unsigned n, i, total = 0;
  uint16_t max, k = 0;
  void **bufs;
  int ret;

  STATS_ADD(ctx, qs_poll, 1);

  max = BATCH_SIZE;
  if (TXBUF_SIZE - ctx->tx_num < max)
    max = TXBUF_SIZE - ctx->tx_num;

  /* allocate buffers contents */
  max = bufcache_prealloc(ctx, max, &handles, &bufs);

  for (n = 0; n < FLEXNIC_PL_APPCTX_NUM && k < max; n++) {
    for (i = 0; i < BATCH_SIZE && k < max; i++) {
      ret = fast_appctx_poll(ctx, ctx->poll_next_ctx, handles[k], bufs[k], ts);

      if (ret == 0)
        k++;
      else if (ret < 0)
        break;

      total++;
    }

    ctx->poll_next_ctx = (ctx->poll_next_ctx + 1) %
      FLEXNIC_PL_APPCTX_NUM;
  }

  /* apply buffer reservations */
  bufcache_alloc(ctx, k);

  STATS_ADD(ctx, qs_total, total);
  if (total == 0)
    STATS_ADD(ctx, qs_empty, total);

  return total;
}

static unsigned poll_kernel(struct dataplane_context *ctx, uint32_t ts)
{
  struct network_buf_handle **handles;
  unsigned total = 0;
  uint16_t max, k = 0;
  void **bufs;
  int ret;

  max = BATCH_SIZE;
  if (TXBUF_SIZE - ctx->tx_num < max)
    max = TXBUF_SIZE - ctx->tx_num;

  max = (max > 8 ? 8 : max);
  /* allocate buffers contents */
  max = bufcache_prealloc(ctx, max, &handles, &bufs);

  for (k = 0; k < max;) {
    ret = fast_kernel_poll(ctx, handles[k], bufs[k], ts);

    if (ret == 0)
      k++;
    else if (ret < 0)
      break;

    total++;
  }

  /* apply buffer reservations */
  bufcache_alloc(ctx, k);

  return total;
}

static unsigned poll_qman(struct dataplane_context *ctx, uint32_t ts)
{
  unsigned q_ids[BATCH_SIZE];
  uint16_t q_bytes[BATCH_SIZE];
  uint8_t q_opaque[BATCH_SIZE];
  struct network_buf_handle **handles;
  void **bufs;
  uint16_t off = 0, max;
  int ret, i, use;

  max = BATCH_SIZE;
  if (TXBUF_SIZE - ctx->tx_num < max)
    max = TXBUF_SIZE - ctx->tx_num;

  STATS_ADD(ctx, qm_poll, 1);

  /* allocate buffers contents */
  max = bufcache_prealloc(ctx, max, &handles, &bufs);

  /* poll queue manager */
  ret = qman_poll(ctx->qman_t, max, q_ids, q_bytes, q_opaque);
  if (ret <= 0) {
    STATS_ADD(ctx, qm_empty, 1);
    return 0;
  }

  STATS_ADD(ctx, qm_total, ret);

  fast_flows_qman_pf(ctx, q_ids, ret);

  for (i = 0; i < ret; i++) {
    use = -1;
    if (q_ids[i] == 0) {
      fprintf(stderr, "poll_qman: unexpected kernel context queue\n");
      abort();
    } else if (q_opaque[i] == APP_QMAN_CTX) {
      fprintf(stderr, "poll_qman: unexpected context queue\n");
      abort();
    } else {
      use = fast_flows_qman(ctx, q_ids[i], handles[off], bufs[off], ts);
    }

    if (use == 0)
     off++;
  }

  /* apply buffer reservations */
  bufcache_alloc(ctx, off);

  return ret;
}

static unsigned poll_qman_fwd(struct dataplane_context *ctx, uint32_t ts)
{
  void *flow_states[BATCH_SIZE];
  int ret, i;

  /* poll queue manager forwarding ring */
  ret = rte_ring_dequeue_burst(ctx->qman_fwd_ring, flow_states, BATCH_SIZE, NULL);
  for (i = 0; i < ret; i++) {
    fast_flows_qman_fwd(ctx, flow_states[i]);
  }

  return ret;
}

static inline uint8_t bufcache_prealloc(struct dataplane_context *ctx, uint16_t num,
    struct network_buf_handle ***handles, void ***bufs)
{
  uint16_t grow, res, head, g, i;
  struct network_buf_handle *nbh;

  /* try refilling buffer cache */
  if (ctx->bufcache_num < num) {
    grow = BUFCACHE_SIZE - ctx->bufcache_num;
    head = (ctx->bufcache_head + ctx->bufcache_num) & (BUFCACHE_SIZE - 1);

    if (head + grow <= BUFCACHE_SIZE) {
      res = network_buf_alloc(ctx->tx_t, grow, ctx->bufcache_bufs + head,
        ctx->bufcache_handles + head);
    } else {
      g = BUFCACHE_SIZE - head;
      res = network_buf_alloc(ctx->tx_t, g, ctx->bufcache_bufs + head,
          ctx->bufcache_handles + head);
      if (res == g) {
        res += network_buf_alloc(ctx->tx_t, grow - g, ctx->bufcache_bufs,
            ctx->bufcache_handles);
      }
    }

    for (i = 0; i < res; i++) {
      g = (head + i) & (BUFCACHE_SIZE - 1);
      nbh = ctx->bufcache_handles[g];
      ctx->bufcache_handles[g] = (struct network_buf_handle *)
        ((uintptr_t) nbh | NBH_TX_BIT);
    }

    ctx->bufcache_num += res;
  }
  num = MIN(num, (ctx->bufcache_head + ctx->bufcache_num <= BUFCACHE_SIZE ?
        ctx->bufcache_num : BUFCACHE_SIZE - ctx->bufcache_head));

  *handles = ctx->bufcache_handles + ctx->bufcache_head;
  *bufs = ctx->bufcache_bufs + ctx->bufcache_head;

  return num;
}

static inline void bufcache_alloc(struct dataplane_context *ctx, uint16_t num)
{
  assert(num <= ctx->bufcache_num);

  ctx->bufcache_head = (ctx->bufcache_head + num) & (BUFCACHE_SIZE - 1);
  ctx->bufcache_num -= num;
}

static inline void bufcache_free(struct dataplane_context *ctx,
    struct network_buf_handle *handle, void *buf)
{
  uintptr_t up = (uintptr_t) handle;
  struct network_buf_handle *nbh;
  uint32_t head, num;

  num = ctx->bufcache_num;
  if ((up & NBH_TX_BIT) == NBH_TX_BIT && num < BUFCACHE_SIZE) {
    /* free to cache */
    head = (ctx->bufcache_head + num) & (BUFCACHE_SIZE - 1);
    ctx->bufcache_bufs[head] = buf;
    ctx->bufcache_handles[head] = handle;
    ctx->bufcache_num = num + 1;
    network_buf_reset(buf);
  } else {
    /* free to network buffer manager */
    nbh = (struct network_buf_handle *) (up & ~NBH_TX_BIT);
    network_free(1, &nbh);
  }
}

static inline void tx_flush(struct dataplane_context *ctx)
{
  int ret;
  unsigned i;

  if (ctx->tx_num == 0) {
    return;
  }

  /* try to send out packets */
  ret = network_send(ctx->tx_t, ctx->tx_num, ctx->tx_offs, ctx->tx_lens,
      ctx->tx_handles);

  if (ret == ctx->tx_num) {
    /* everything sent */
    ctx->tx_num = 0;
  } else if (ret > 0) {
    /* move unsent packets to front */
    for (i = ret; i < ctx->tx_num; i++) {
      ctx->tx_offs[i - ret] = ctx->tx_offs[i];
      ctx->tx_lens[i - ret] = ctx->tx_lens[i];
      ctx->tx_handles[i - ret] = ctx->tx_handles[i];
    }
    ctx->tx_num -= ret;
  }
}

static void poll_scale(struct dataplane_context *ctx)
{
  unsigned st = fp_scale_to;

  if (st == 0)
    return;

  fprintf(stderr, "Scaling fast path from %u to %u\n", fp_cores_cur, st);
  if (st < fp_cores_cur) {
    if (network_scale_down(fp_cores_cur, st) != 0) {
      fprintf(stderr, "network_scale_down failed\n");
      abort();
    }
  } else if (st > fp_cores_cur) {
    if (network_scale_up(fp_cores_cur, st) != 0) {
      fprintf(stderr, "network_scale_up failed\n");
      abort();
    }
  } else {
    fprintf(stderr, "poll_scale: warning core number didn't change\n");
  }

  fp_cores_cur = st;
  fp_scale_to = 0;
}
