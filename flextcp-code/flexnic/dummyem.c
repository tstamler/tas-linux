#include <assert.h>
#include <rte_config.h>
#include <rte_malloc.h>
#include <rte_cycles.h>

#include <flextcp_plif.h>

#include "internal.h"
#include "tcp_common.h"

#define BATCH_SIZE 32
#define BUF_SIZE 2048

struct dataplane_context {
  struct dma_thread *dma_t;
  void *rx_buf;
  void *tx_buf;

  uint8_t dbs[BATCH_SIZE][64] __attribute__((aligned(64)));
};


static unsigned num_threads;
struct flextcp_pl_mem *pl_memory = NULL;
struct dataplane_context **ctxs = NULL;

static rte_spinlock_t flow_spins[FLEXNIC_PL_FLOWST_NUM] =
    { RTE_SPINLOCK_INITIALIZER };

static unsigned poll_doorbells(struct dataplane_context *ctx);

static void dummy_kernel_db(struct dataplane_context *ctx, void *db);
static void dummy_kernel_packet(struct dataplane_context *ctx, void *buf,
    size_t len);
static inline int krxq_packet(struct flextcp_pl_krx *krx, uint16_t data_len);
static inline int krxq_put(struct flextcp_pl_krx *krx, uint16_t data_len,
    void *data);

static void dummy_appctx_db(struct dataplane_context *ctx, void *db,
    uint32_t db_id);
static inline int actx_txq_fetch(struct dataplane_context *ctx, uint32_t db_id,
    struct flextcp_pl_appctx *actx, struct flextcp_pl_atx *atx);
static int actx_rxq_add(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx, struct flextcp_pl_arx *arx);

static void dummy_flows_bump(struct dataplane_context *ctx, uint32_t flow_id,
    uint32_t bump_seq, uint32_t rx_tail, uint32_t tx_head);

static void touch_txbuf(struct dataplane_context *ctx, uint64_t base,
    size_t len);

int dataplane_init(unsigned threads)
{
  num_threads = threads;

  pl_memory = internal_mem;
  if (FLEXNIC_INTERNAL_MEM_SIZE < sizeof(struct flextcp_pl_mem)) {
    fprintf(stderr, "dataplane_init: internal flexnic memory size not "
        "sufficient (got %x, need %zx)\n", FLEXNIC_INTERNAL_MEM_SIZE,
        sizeof(struct flextcp_pl_mem));
    return -1;
  }

  if ((ctxs = calloc(threads, sizeof(*ctxs))) == NULL) {
    perror("datplane_init: calloc failed");
    return -1;
  }

  return 0;
}

struct dataplane_context *dataplane_context_init(struct network_rx_thread *rx_t,
    struct network_tx_thread *tx_t, struct dma_thread *dma_t,
    struct qman_thread *qman_t)
{
  static volatile size_t ctxnum = 0;
  struct dataplane_context *ctx;

  if ((ctx = rte_zmalloc("dataplane context", sizeof(*ctx), 0)) == NULL) {
    return NULL;
  }

  if ((ctx->rx_buf = rte_zmalloc("dataplane context buffer", BUF_SIZE, 0))
          == NULL ||
      (ctx->tx_buf = rte_zmalloc("dataplane context buffer", BUF_SIZE, 0))
          == NULL)
  {
    rte_free(ctx->rx_buf);
    rte_free(ctx);
    return NULL;
  }

  ctx->dma_t = dma_t;
  ctxs[__sync_fetch_and_add(&ctxnum, 1)] = ctx;

  return ctx;
}

void dataplane_context_destroy(struct dataplane_context *ctx)
{
  rte_free(ctx->rx_buf);
  rte_free(ctx->tx_buf);
  rte_free(ctx);
}

void dataplane_loop(struct dataplane_context *ctx)
{
  while (!exited) {
    poll_doorbells(ctx);
  }
}

static inline uint64_t read_stat(uint64_t *p)
{
  return __sync_lock_test_and_set(p, 0);
}

void dataplane_dump_stats(void)
{
}

static unsigned poll_doorbells(struct dataplane_context *ctx)
{
  unsigned n, i;
  unsigned ids[BATCH_SIZE];
  void *bufs[BATCH_SIZE];

  /* array of pointers to doorbell buffers */
  /* TODO: move to ctx */
  for (i = 0; i < BATCH_SIZE; i++) {
    bufs[i] = ctx->dbs[i];
  }

  /* poll doorbells */
  n = dma_db_poll(ctx->dma_t, BATCH_SIZE, bufs, ids, 0);
  if (n <= 0) {
    return 0;
  }

  for (i = 0; i < n; i++) {
    if (ids[i] == 0) {
      dummy_kernel_db(ctx, ctx->dbs[i]);
    } else {
      dummy_appctx_db(ctx, ctx->dbs[i], ids[i]);
    }
  }
  return 0;
}

/*****************************************************************************/

static void dummy_kernel_db(struct dataplane_context *ctx, void *db)
{
  struct flextcp_pl_kdb *kdb = db;
  struct flextcp_pl_ktx *ktx;
  uint32_t pos, limit, len;
  uint8_t *b = ctx->rx_buf;

  /* validate queue bumps */
  if (kdb->msg.bumpqueue.rx_tail >= pl_memory->kctx.rx_len ||
      kdb->msg.bumpqueue.tx_tail >= pl_memory->kctx.tx_len)
  {
    fprintf(stderr, "dummy_kernel_db: bump beyond queue length rxtail "
        "(new=%u old=%u)  txtail (new=%u old=%u)\n",
        kdb->msg.bumpqueue.rx_tail, pl_memory->kctx.rx_tail,
        kdb->msg.bumpqueue.tx_tail, pl_memory->kctx.tx_tail);
    abort();
  }

  /* update rx and tx tail */
  pl_memory->kctx.rx_tail = kdb->msg.bumpqueue.rx_tail;
  pl_memory->kctx.tx_tail = kdb->msg.bumpqueue.tx_tail;

  do {
    /* figure out maximum address ready to be read */
    pos = pl_memory->kctx.tx_head;
    limit = pl_memory->kctx.tx_tail;
    if (limit < pos) {
      limit = pl_memory->kctx.tx_len;
    }

    /* Length we're actually going to read */
    len = 1800;
    if (limit - pos < len) {
      len = limit - pos;
    }

    /* Abort if there is no entries on this queue to be fetched */
    if (len == 0) {
      return;
    }

    /* Read transmit queue entry */
    if (dma_read(pl_memory->kctx.tx_base + pos, len, b) != 0) {
      fprintf(stderr, "kernel_qman: DMA read failed addr=%"PRIx64" len=%x\n",
          pl_memory->kctx.tx_base + pos, len);
      abort();
    }

    /* handle tx queue entry */
    ktx = (struct flextcp_pl_ktx *) b;
    pos += ktx->len + ktx->skip_extra;
    if (ktx->type == FLEXTCP_PL_KTX_DUMMY) {
      /* do nothing */
      //fprintf(stderr, "dummy\n");
    } else if (ktx->type == FLEXTCP_PL_KTX_PACKET) {
      /* send out packet */
      /*fprintf(stderr, "packet\n");*/
      dummy_kernel_packet(ctx, b + sizeof(*ktx), ktx->len - sizeof(*ktx));
    } else if (ktx->type == FLEXTCP_PL_KTX_CONNRETRAN) {
      /* do nothing */
      /*fprintf(stderr, "retran\n");*/
    } else {
      fprintf(stderr, "kernel_qman: unknown type=%u\n", ktx->type);
      abort();
    }

    /* bump head position */
    if (pos == pl_memory->kctx.tx_len) {
      pl_memory->kctx.tx_head = 0;
    } else {
      pl_memory->kctx.tx_head = pos;
    }
  } while (1);
}

static void dummy_kernel_packet(struct dataplane_context *ctx, void *buf,
    size_t len)
{
  struct flextcp_pl_krx *krx;
  struct pkt_tcp *prx, *ptx;
  struct pkt_arp *aprx, *aptx;
  struct tcp_timestamp_opt *ts;
  uint16_t flags;
  struct tcp_opts opts;

  ptx = buf;
  aptx = buf;

  krx = ctx->tx_buf;
  prx = (struct pkt_tcp *) (krx + 1);
  aprx = (struct pkt_arp *) (krx + 1);

  if (f_beui16(ptx->eth.type) == ETH_TYPE_ARP &&
      f_beui16(aptx->arp.oper) == ARP_OPER_REQUEST)
  {
    /* arp response */
    *aprx = *aptx;
    aprx->arp.oper = t_beui16(ARP_OPER_REPLY);
    aprx->eth.src = aptx->eth.dest;
    aprx->eth.dest = aptx->eth.src;
    aprx->arp.spa = aptx->arp.tpa;
    aprx->arp.tpa = aptx->arp.spa;
    aprx->arp.tha = aptx->arp.sha;
    memset(&aprx->arp.sha, 1, 6);

    krxq_packet(krx, sizeof(*aprx));
  } else if (f_beui16(ptx->eth.type) == ETH_TYPE_IP &&
      ptx->ip.proto == IP_PROTO_TCP)
  {
    if (tcp_parse_options(ptx, len, &opts) != 0) {
      fprintf(stderr, "dummy_kernel_packet: parsing tcp options failed\n");
      return;
    }

    /* tcp response */
    memcpy(prx, ptx, sizeof (*prx) + 4 * (TCPH_HDRLEN(&ptx->tcp) - 5));
    prx->eth.src = ptx->eth.dest;
    prx->eth.dest = ptx->eth.src;
    prx->ip.src = ptx->ip.dest;
    prx->ip.dest = ptx->ip.src;
    prx->tcp.src = ptx->tcp.dest;
    prx->tcp.dest = ptx->tcp.src;
    prx->tcp.seqno = ptx->tcp.ackno;
    prx->tcp.ackno = ptx->tcp.seqno;

    /* echo timestamp */
    if (opts.ts != NULL) {
      ts = (struct tcp_timestamp_opt *) ((uint8_t *) prx +
          ((uint8_t *) opts.ts - (uint8_t *) ptx));
      ts->ts_ecr = ts->ts_val;
    }

    flags = TCPH_FLAGS(&ptx->tcp) & (TCP_SYN | TCP_ACK);
    if (flags == TCP_SYN) {
      /*fprintf(stderr, "dummy_kernel_packet: echo SYN packet\n");*/
      TCPH_SET_FLAG(&prx->tcp, TCP_ACK);
      krxq_packet(krx, len);
    } else if (flags == TCP_ACK) {
      /*fprintf(stderr, "dummy_kernel_packet: received ACK packet\n");*/
    } else {
      fprintf(stderr, "dummy_kernel_packet: unexpected TCP flags: %x\n",
          TCPH_FLAGS(&ptx->tcp));
    }
  }
}

static inline int krxq_packet(struct flextcp_pl_krx *krx, uint16_t data_len)
{
  krx->type = FLEXTCP_PL_KRX_PACKET;
  return krxq_put(krx, data_len, krx + 1);
}

static inline int krxq_put(struct flextcp_pl_krx *krx, uint16_t data_len,
    void *data)
{
  uint32_t limit, head, end_avail;
  uint8_t extra_skip;
  struct flextcp_pl_krx krx_dummy;
  int ret = 0;
  struct flextcp_pl_appctx *kctx = &pl_memory->kctx;
  uint16_t krx_len = sizeof(*krx) + data_len;

  /* lock rxq */
  head = kctx->rx_head;
  limit = kctx->rx_tail;

  /* handle wrap around */
  if (limit <= head) {
    limit = kctx->rx_len;

    /* add dummy entry if packet doesn't fit before the wrap */
    if (limit - head < krx_len) {
      assert(limit - head >= sizeof(krx_dummy));

      /* fill in dummy queue element */
      krx_dummy.len = limit - head;
      krx_dummy.skip_extra = 0;
      krx_dummy.type = FLEXTCP_PL_KRX_DUMMY;

      /* DMA write */
      if (dma_write(kctx->rx_base + head, sizeof(krx_dummy), &krx_dummy) != 0) {
        fprintf(stderr, "krxq_put: DMA write failed addr=%"PRIx64" len=%zu\n",
            kctx->rx_base + head, sizeof(krx_dummy));
        abort();
      }

      kctx->rx_head = head = 0;
      limit = kctx->rx_tail;
    }
  }

  /* make sure there is room */
  if (limit - head < krx_len) {
    fprintf(stderr, "krxq_put: dropping due to lack of kernel queue space\n");
    ret = -1;
    goto out;
  }

  /* add extra skip if there wouldn't be enough space after this entry to the
   * queue end for a dummy entry */
  extra_skip = 0;
  end_avail = kctx->rx_len - (head + krx_len);
  if (end_avail > 0 && end_avail < sizeof(*krx)) {
    extra_skip = end_avail;
  }

  /* update queue head */
  if (head + krx_len + extra_skip == kctx->rx_len) {
    kctx->rx_head = 0;
  } else {
    kctx->rx_head = head + krx_len + extra_skip;
  }

  /* fill in queue header */
  krx->len = krx_len;
  krx->skip_extra = extra_skip;
  krx->ktx_head = pl_memory->kctx.tx_head;

  /* DMA writes, first data tehn header */
  if (dma_write(kctx->rx_base + head + sizeof(*krx), data_len, data) != 0) {
    fprintf(stderr, "krxq_put: DMA write data failed addr=%"PRIx64" len=%u\n",
        kctx->rx_base + head + sizeof(*krx), data_len);
    abort();
  }
  if (dma_write(kctx->rx_base + head, sizeof(*krx), krx) != 0) {
    fprintf(stderr, "krxq_put: DMA write data failed addr=%"PRIx64" len=%zu\n",
        kctx->rx_base + head, sizeof(*krx));
    abort();
  }

out:
  return ret;
}

/*****************************************************************************/

static void dummy_appctx_db(struct dataplane_context *ctx, void *db,
    uint32_t db_id)
{
  struct flextcp_pl_adb *adb = db;
  struct flextcp_pl_appctx *actx = &pl_memory->appctx[db_id];
  struct flextcp_pl_atx atx;
  uint32_t tx_tail, tx_bump, i;
  int ret;

  actx->rx_tail = adb->msg.bumpqueue.rx_tail;

  /* update queue manager */
  tx_tail = adb->msg.bumpqueue.tx_tail;
  if (actx->tx_tail != tx_tail) {
    /* figure out how many tx entries are to be added */
    if (tx_tail > actx->tx_tail) {
      tx_bump = tx_tail - actx->tx_tail;
    } else {
      tx_bump = tx_tail + actx->tx_len - actx->tx_tail;
    }
    tx_bump /= sizeof(struct flextcp_pl_atx);

    actx->tx_tail = tx_tail;
  } else {
    tx_bump = 0;
  }

  for (i = 0; i < tx_bump; i++) {
    ret = actx_txq_fetch(ctx, db_id, actx, &atx);
    if (ret != 0) {
      fprintf(stderr, "dummy_appctx_db: spurious fetch UNEXPECTED\n");
      abort();
    }

    if (atx.type == FLEXTCP_PL_ATX_CONNUPDATE) {
      /* update RX/TX queue pointers for connection */
      if (atx.msg.connupdate.flow_id >= FLEXNIC_PL_FLOWST_NUM) {
        fprintf(stderr, "dummy_appctx_db: invalid flow id=%u\n",
            atx.msg.connupdate.flow_id);
        abort();
      }

      dummy_flows_bump(ctx, atx.msg.connupdate.flow_id,
          atx.msg.connupdate.bump_seq, atx.msg.connupdate.rx_tail,
          atx.msg.connupdate.tx_head);
    } else {
      fprintf(stderr, "dummy_appctx_db: unknown type: %u\n", atx.type);
      abort();
    }
  }
}

/* Requires actx lock to be held */
static inline int actx_txq_fetch(struct dataplane_context *ctx, uint32_t db_id,
    struct flextcp_pl_appctx *actx, struct flextcp_pl_atx *atx)
{
  struct flextcp_pl_arx arx;
  uint32_t tx_hl, up_diff;

  if (actx->tx_head == actx->tx_tail) {
    return -1;
  }

  if (dma_read(actx->tx_base + actx->tx_head, sizeof(*atx), atx) != 0) {
    fprintf(stderr, "actx_txq_fetch: DMA read failed addr=%"PRIx64"\n",
        actx->tx_base + actx->tx_head);
    abort();
  }

  actx->tx_head += sizeof(struct flextcp_pl_atx);
  if (actx->tx_head >= actx->tx_len) {
    actx->tx_head -= actx->tx_len;
  }

  /* check whether tx queue is more than half full since last rx queue entry.
   * In this case we need to add a dummy entry to deal with corner cases for
   * object steering where a core might not get rx notifications even if it is
   * sending. */
  tx_hl = actx->tx_head_last;
  if (tx_hl <= actx->tx_head) {
    up_diff = actx->tx_head - tx_hl;
  } else {
    up_diff = actx->tx_head + actx->tx_len - tx_hl;
  }
  if (up_diff >= actx->tx_len / 2) {
    /* need to add a dummy rx queue entry */
    arx.type = FLEXTCP_PL_ARX_DUMMY;
    if (actx_rxq_add(ctx, actx, &arx) != 0) {
      fprintf(stderr, "actx_txq_fetch: no space in app rx queue\n");
    }
  }

  return 0;
}

/* Requires actx lock to be held */
static int actx_rxq_add(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx, struct flextcp_pl_arx *arx)
{

  uint32_t rxhead, rxnhead, txhead;

  txhead = actx->tx_head;
  MEM_BARRIER();

  rxhead = actx->rx_head;

  rxnhead = rxhead + sizeof(*arx);
  if (rxnhead >= actx->rx_len) {
    rxnhead -= actx->rx_len;
  }

  if (rxnhead == actx->rx_tail) {
    /* queue is full */
    return -1;
  }
  actx->rx_head = rxnhead;
  actx->tx_head_last = txhead;

  arx->tx_head = txhead;
  if (dma_write(actx->rx_base + rxhead, sizeof(*arx), arx) != 0) {
    fprintf(stderr, "actx_rxq_add: DMA write failed addr=%"PRIx64"\n",
        actx->rx_base + rxhead);
    abort();
  }
  return 0;
}

/*****************************************************************************/

static void dummy_flows_bump(struct dataplane_context *ctx, uint32_t flow_id,
    uint32_t bump_seq, uint32_t rx_tail, uint32_t tx_head)
{
  struct flextcp_pl_flowst *fs = &pl_memory->flowst[flow_id];
  struct flextcp_pl_appctx *actx;
  uint32_t tail, tx_bump;
  struct flextcp_pl_arx arx;

  rte_spinlock_lock(&flow_spins[flow_id]);

  /* catch out of order bumps */
  if ((bump_seq >= fs->bump_seq &&
        bump_seq - fs->bump_seq > (UINT32_MAX / 2)) ||
      (bump_seq < fs->bump_seq &&
       (fs->bump_seq < ((UINT32_MAX / 4) * 3) ||
       bump_seq > (UINT32_MAX / 4))))
  {
    goto unlock;
  }
  fs->bump_seq = bump_seq;

  /* update flow state */
  fs->tx_head = tx_head;
  tail = fs->rx_next_pos + fs->rx_avail;
  if (tail >= fs->rx_len) {
    tail -= fs->rx_len;
  }
  if (rx_tail >= tail) {
    fs->rx_avail += rx_tail - tail;
  } else {
    fs->rx_avail += fs->rx_len - tail + rx_tail;
  }

  /* */
  if (fs->tx_next_pos <= fs->tx_head) {
    tx_bump = fs->tx_head - fs->tx_next_pos;
    touch_txbuf(ctx, fs->tx_base + fs->tx_next_pos, tx_bump);
  } else {
    tx_bump = fs->tx_len - fs->tx_next_pos + fs->tx_head;
    touch_txbuf(ctx, fs->tx_base + fs->tx_next_pos,
        fs->tx_len - fs->tx_next_pos);
    touch_txbuf(ctx, fs->tx_base, fs->tx_head);
  }
  fs->tx_next_seq += tx_bump;
  fs->tx_next_pos += tx_bump;
  if (fs->tx_next_pos >= fs->tx_len) {
    fs->tx_next_pos -= fs->tx_len;
  }

  if (tx_bump > 0) {
    if (!(fs->rx_base_sp & FLEXNIC_PL_FLOWST_OBJCONN)) {
      arx.type = FLEXTCP_PL_ARX_CONNUPDATE;
    } else {
      arx.type = FLEXTCP_PL_ARX_OBJUPDATE;
    }

    arx.msg.connupdate.opaque = fs->opaque;
    arx.msg.connupdate.rx_bump = 0;
    arx.msg.connupdate.rx_pos = fs->rx_next_pos;
    arx.msg.connupdate.tx_bump = tx_bump;

    actx = &pl_memory->appctx[fs->db_id];
    if (actx_rxq_add(ctx, actx, &arx) != 0) {
      /* TODO: how do we handle this? */
      fprintf(stderr, "dma_krx_pkt_fastpath: no space in app rx queue\n");
    }
  }

unlock:
  rte_spinlock_unlock(&flow_spins[flow_id]);
}

static void touch_txbuf(struct dataplane_context *ctx, uint64_t base,
    size_t len)
{
  size_t l;

  while (len > 0) {
    l = (len <= BUF_SIZE ? len : BUF_SIZE);

    if (dma_read(base, l, ctx->tx_buf) != 0) {
      fprintf(stderr, "touch_txbuf: dma_read(%"PRIx64", %zu) failed\n",
          base, l);
      abort();
    }

    len -= l;
    base += len;
  }
}
