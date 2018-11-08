#ifndef FASTEMU_H_
#define FASTEMU_H_

#include <fastemu.h>

extern struct flextcp_pl_mem *pl_memory;
extern struct dataplane_context **ctxs;

/*****************************************************************************/
/* fast_kernel.c */
int fast_kernel_poll(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, void *buf, uint32_t ts);
void fast_kernel_packet(struct dataplane_context *ctx, void *buf,
    uint16_t off, uint16_t len, struct network_buf_handle *nbh);

/* fast_appctx.c */
int fast_appctx_poll(struct dataplane_context *ctx, uint32_t id,
    struct network_buf_handle *nbh, void *buf, uint32_t ts);
int fast_actx_rxq_add(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx, struct flextcp_pl_arx *arx);
void fast_actx_rxq_pf(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx);

/* fast_flows.c */
void fast_flows_qman_pf(struct dataplane_context *ctx, uint32_t *queues,
    uint16_t n);
int fast_flows_qman(struct dataplane_context *ctx, uint32_t queue,
    struct network_buf_handle *nbh, void *buf, uint32_t ts);
int fast_flows_qman_fwd(struct dataplane_context *ctx,
    struct flextcp_pl_flowst *fs);
int fast_flows_packet(struct dataplane_context *ctx, void *buf,
    uint16_t off, uint16_t len, struct network_buf_handle *nbh, void *fs,
    uint32_t ts);
void fast_flows_packet_fss(struct dataplane_context *ctx, void **bufs,
    uint16_t *offs, void **fss, uint16_t n);

int flast_flows_bump(struct dataplane_context *ctx, uint32_t flow_id,
    uint32_t bump_seq, uint32_t rx_tail, uint32_t tx_head,
    struct network_buf_handle *nbh, void *buf, uint32_t ts);
void fast_flows_retransmit(struct dataplane_context *ctx, uint32_t flow_id);

/*****************************************************************************/
/* Helpers */

static inline void tx_send(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, uint16_t off, uint16_t len)
{
  uint32_t i = ctx->tx_num;

  if (i >= TXBUF_SIZE) {
    fprintf(stderr, "tx_send: transmit buffer full, unexpected\n");
    abort();
  }

  ctx->tx_offs[i] = off;
  ctx->tx_lens[i] = len;
  ctx->tx_handles[i] = (struct network_buf_handle *)
    ((uintptr_t) nbh & ~NBH_TX_BIT);
  ctx->tx_num = i + 1;
}

static inline uint16_t tx_xsum_enable(struct network_buf_handle *nbh,
    struct ip_hdr *iph)
{
  nbh = (struct network_buf_handle *) ((uintptr_t) nbh & ~NBH_TX_BIT);
  return network_buf_tcpxsums((struct network_buf_handle *)
      ((uintptr_t) nbh & ~NBH_TX_BIT), sizeof(struct eth_hdr), sizeof(*iph),
      iph);
}

#endif /* ndef FASTEMU_H_ */
