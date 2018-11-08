#include <assert.h>
#include <rte_config.h>

#include <flextcp_plif.h>

#include "internal.h"
#include "fastemu.h"

int fast_appctx_poll(struct dataplane_context *ctx, uint32_t id,
    struct network_buf_handle *nbh, void *buf, uint32_t ts)
{
  struct flextcp_pl_appctx *actx = &pl_memory->appctx[ctx->id][id];
  struct flextcp_pl_atx *atx;
  void *ptr;
  int ret;
  uint8_t type;

  /* stop if context is not in use */
  if (actx->tx_len == 0)
    return -1;

  if (dma_pointer(actx->tx_base + actx->tx_head, sizeof(*atx), &ptr) != 0) {
    fprintf(stderr, "poll_queues: invalid queue address\n");
    abort();
  }
  atx = ptr;

  type = atx->type;
  MEM_BARRIER();

  if (type == 0) {
    return -1;
  } else if (type == FLEXTCP_PL_ATX_CONNUPDATE) {
    /* update RX/TX queue pointers for connection */

    if (atx->msg.connupdate.flow_id >= FLEXNIC_PL_FLOWST_NUM) {
      fprintf(stderr, "fast_appctx_poll: invalid flow id=%u\n",
          atx->msg.connupdate.flow_id);
      abort();
    }

    ret = flast_flows_bump(ctx, atx->msg.connupdate.flow_id,
        atx->msg.connupdate.bump_seq, atx->msg.connupdate.rx_tail,
        atx->msg.connupdate.tx_head, nbh, buf, ts);

    if (ret != 0)
      ret = 1;
  } else {
    fprintf(stderr, "fast_appctx_poll: unknown type: %u id=%u\n", type,
        id);
    abort();
  }

  MEM_BARRIER();
  atx->type = 0;

  actx->tx_head += sizeof(*atx);
  if (actx->tx_head >= actx->tx_len)
    actx->tx_head -= actx->tx_len;

  return ret;
}

int fast_actx_rxq_add(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx, struct flextcp_pl_arx *arx)
{
  struct flextcp_pl_arx *parx;
  void *ptr;
  uint32_t rxhead, rxnhead, txhead;
  int ret = 0;

  txhead = actx->tx_head;
  MEM_BARRIER();

  rxhead = actx->rx_head;

  rxnhead = rxhead + sizeof(*arx);
  if (rxnhead >= actx->rx_len) {
    rxnhead -= actx->rx_len;
  }

  if (dma_pointer(actx->rx_base + rxhead, sizeof(*arx), &ptr) != 0) {
    fprintf(stderr, "fast_actx_rxq_add: invalid queue address\n");
    abort();
  }
  parx = ptr;

  if (parx->type != 0) {
    /* queue is full */
    ret = -1;
    goto out;
  }
  actx->rx_head = rxnhead;

  arx->tx_head = txhead;
  *parx = *arx;

  util_flexnic_kick(actx);

out:
  return ret;
}

void fast_actx_rxq_pf(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx)
{
  void *ptr;

  if (dma_pointer(actx->rx_base + actx->rx_head, sizeof(struct flextcp_pl_arx),
        &ptr) != 0)
  {
    fprintf(stderr, "fast_actx_rxq_pf: invalid queue address\n");
    abort();
  }

  rte_prefetch0(ptr);
}
