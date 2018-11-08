#ifndef FASTEMU_H
#define FASTEMU_H

#include <rte_interrupts.h>

#define BATCH_SIZE 32
#define DB_OFF 256
#define BUFCACHE_SIZE 128
#define NBH_TX_BIT (1ULL << 63)
#define TXBUF_SIZE (2 * BATCH_SIZE)

#define KDB_QMAN_ID 0
#define APP_QMAN_CTX 0
#define APP_QMAN_FLOW 1

struct dataplane_context {
  struct network_rx_thread *rx_t;
  struct network_tx_thread *tx_t;
  struct qman_thread *qman_t;
  struct rte_ring *qman_fwd_ring;
  uint16_t id;
  int evfd;
  struct rte_epoll_event ev;

  /********************************************************/
  /* send buffer */
  struct network_buf_handle *tx_handles[TXBUF_SIZE];
  uint16_t tx_offs[TXBUF_SIZE];
  uint16_t tx_lens[TXBUF_SIZE];
  uint16_t tx_num;

  /********************************************************/
  /* polling queues */
  uint32_t poll_next_ctx;

  /********************************************************/
  /* pre-allocated buffers for polling doorbells and queue manager */
  struct network_buf_handle *bufcache_handles[BUFCACHE_SIZE];
  void *bufcache_bufs[BUFCACHE_SIZE];
  uint16_t bufcache_num;
  uint16_t bufcache_head;

  uint64_t loadmon_cyc_busy;

#ifdef DATAPLANE_STATS
  /********************************************************/
  /* Stats */
  uint64_t stat_qm_poll;
  uint64_t stat_qm_empty;
  uint64_t stat_qm_total;

  uint64_t stat_rx_poll;
  uint64_t stat_rx_empty;
  uint64_t stat_rx_total;

  uint64_t stat_qs_poll;
  uint64_t stat_qs_empty;
  uint64_t stat_qs_total;

  uint64_t stat_cyc_db;
  uint64_t stat_cyc_qm;
  uint64_t stat_cyc_rx;
  uint64_t stat_cyc_qs;
#endif
};

#endif
