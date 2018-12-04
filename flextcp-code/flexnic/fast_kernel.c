#include <assert.h>
#include <unistd.h>
#include <rte_config.h>

#include <flextcp_plif.h>
#include <utils_timeout.h>

#include "internal.h"
#include "fastemu.h"
#include "tcp_common.h"

extern int kernel_notifyfd;

static inline void inject_tcp_ts(void *buf, uint16_t len, uint32_t ts,
    struct network_buf_handle *nbh);

int fast_kernel_poll(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, void *buf, uint32_t ts)
{
  struct flextcp_pl_appctx *kctx = &pl_memory->kctx[ctx->id];
  struct flextcp_pl_ktx *ktx;
  uint32_t flow_id, len;
  void *ptr;
  int ret = -1;

  /* stop if context is not in use */
  if (kctx->tx_len == 0)
    return -1;

  if (dma_pointer(kctx->tx_base + kctx->tx_head, sizeof(*ktx), &ptr) != 0) {
    fprintf(stderr, "fast_kernel_poll: invalid queue address\n");
    abort();
  }
  ktx = ptr;

  if (ktx->type == 0) {
    return -1;
  } else if (ktx->type == FLEXTCP_PL_KTX_PACKET) {
    len = ktx->msg.packet.len;

    fprintf(stderr, "reading packet\n");
    /* Read transmit queue entry */
    if (dma_read(ktx->msg.packet.addr, len, buf) != 0) {
      fprintf(stderr, "kernel_qman: DMA read failed addr=%"PRIx64" len=%x\n",
          ktx->msg.packet.addr, len);
      abort();
    }

    ret = 0;
    inject_tcp_ts(buf, len, ts, nbh);
    fprintf(stderr, "sending packet\n");
    tx_send(ctx, nbh, 0, len);
  } else if (ktx->type == FLEXTCP_PL_KTX_CONNRETRAN) {
    flow_id = ktx->msg.connretran.flow_id;
    if (flow_id >= FLEXNIC_PL_FLOWST_NUM) {
      fprintf(stderr, "fast_kernel_qman: invalid flow id=%u\n", flow_id);
      abort();
    }

    fast_flows_retransmit(ctx, flow_id);
    ret = 1;
  } else {
    fprintf(stderr, "fast_appctx_poll: unknown type: %u\n", ktx->type);
    abort();
  }

  MEM_BARRIER();
  ktx->type = 0;

  kctx->tx_head += sizeof(*ktx);
  if (kctx->tx_head >= kctx->tx_len)
    kctx->tx_head -= kctx->tx_len;

  return ret;
}

static void fast_kernel_kick(void)
{
  static uint32_t __thread last_ts = 0;
  uint32_t now = util_timeout_time_us();

  /* fprintf(stderr, "kicking kernel?\n"); */

  if(now - last_ts > POLL_CYCLE) {
    // Kick kernel
    /* fprintf(stderr, "kicking kernel\n"); */
    assert(kernel_notifyfd != 0);
    uint64_t val = 1;
    int r = write(kernel_notifyfd, &val, sizeof(uint64_t));
    assert(r == sizeof(uint64_t));
  }

  last_ts = now;
}

void fast_kernel_packet(struct dataplane_context *ctx, void *buf,
    uint16_t off, uint16_t len, struct network_buf_handle *nbh)
{
  struct flextcp_pl_appctx *kctx = &pl_memory->kctx[ctx->id];
  struct flextcp_pl_krx *krx;
  void *ptr;

  /* queue not initialized yet */
  if (kctx->rx_len == 0) {
    return;
  }

  fprintf(stderr, "sending packet to kernel\n");
  if (dma_pointer(kctx->rx_base + kctx->rx_head, sizeof(*krx), &ptr) != 0) {
    fprintf(stderr, "fast_kernel_packet: invalid queue address\n");
    abort();
  }
  krx = ptr;

  /* queue full */
  if (krx->type != 0) {
    return;
  }

  kctx->rx_head += sizeof(*krx);
  if (kctx->rx_head >= kctx->rx_len)
    kctx->rx_head -= kctx->rx_len;

  if (dma_write(krx->addr, len, (uint8_t *) buf + off) != 0) {
    fprintf(stderr, "fast_kernel_packet: DMA write failed addr=%"PRIx64
        " len=%u\n", krx->addr, len);
    abort();
  }

  if (network_buf_flowgroup(nbh, &krx->msg.packet.flow_group)) {
    fprintf(stderr, "fast_kernel_packet: network_buf_flowgroup failed\n");
    abort();
  }

  krx->msg.packet.len = len;
  krx->msg.packet.fn_core = ctx->id;
  MEM_BARRIER();

  /* krx queue header */
  krx->type = FLEXTCP_PL_KRX_PACKET;
  fast_kernel_kick();
}

static inline void inject_tcp_ts(void *buf, uint16_t len, uint32_t ts,
    struct network_buf_handle *nbh)
{
  struct pkt_tcp *p = buf;
  struct tcp_opts opts;

  if (len < sizeof(*p) || f_beui16(p->eth.type) != ETH_TYPE_IP ||
      p->ip.proto != IP_PROTO_TCP)
  {
    return;
  }

  if (tcp_parse_options(buf, len, &opts) != 0) {
    fprintf(stderr, "inject_tcp_ts: parsing options failed\n");
    return;
  }

  if (opts.ts == NULL) {
    fprintf(stderr, "inject_tcp_ts: no timestamp option\n");
    return;
  }

  opts.ts->ts_val = t_beui32(ts);

  p->ip.chksum = 0;
  p->tcp.chksum = tx_xsum_enable(nbh, &p->ip);
}
