#include <stdio.h>
#include <assert.h>

#include <rte_config.h>
#include <rte_memcpy.h>
#include <rte_malloc.h>
#include <rte_lcore.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_spinlock.h>
#include <rte_ip.h>

#include <utils.h>
#include <utils_rng.h>
#include <flextcp_plif.h>
#include "internal.h"

#define PERTHREAD_MBUFS 1024
#define MBUF_SIZE (BUFFER_SIZE + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define RX_DESCRIPTORS 256
#define TX_DESCRIPTORS 128

//#define EMULATE_DROPS 10000

struct network_rx_thread {
  struct rte_mempool *pool;
  uint16_t queue_id;
#ifdef EMULATE_DROPS
  struct utils_rng drop_rng;
#endif
};

struct network_tx_thread {
  struct rte_mempool *pool;
  uint16_t queue_id;
};

static int device_running = 0;
static const uint8_t port_id = 0;
static const struct rte_eth_conf port_conf = {
    .rxmode = {
      //.split_hdr_size = 0,
      //.header_split   = 0, /* Header Split disabled */
      //.hw_ip_checksum = 0,
      //.hw_vlan_filter = 0, /* VLAN filtering disabled */
      //.jumbo_frame    = 0, /* Jumbo Frame Support disabled */
      //.hw_strip_crc   = 0, /* CRC stripped by hardware */
      .mq_mode = ETH_MQ_RX_RSS,
      .offloads = 0,
    },
    .txmode = {
      .mq_mode = ETH_MQ_TX_NONE,
      .offloads = 0,
    },
    .rx_adv_conf = {
      .rss_conf = {
        .rss_hf = ETH_RSS_TCP,
      },
    },
    .intr_conf = {
      .rxq = 1,
    },
  };

static unsigned num_rx_threads;
static unsigned num_tx_threads;
static volatile unsigned next_rx_id;
static volatile unsigned next_tx_id;
static struct network_rx_thread **rx_threads;
static struct network_tx_thread **tx_threads;

static struct rte_eth_dev_info eth_devinfo;
struct ether_addr eth_addr;

static uint16_t rss_reta_size;
static struct rte_eth_rss_reta_entry64 *rss_reta = NULL;
static uint16_t *rss_core_buckets = NULL;

static struct rte_mempool *mempool_alloc(void);
static int reta_setup(void);

int network_init(unsigned num_rx, unsigned num_tx)
{
  uint8_t count;
  int ret;

  num_rx_threads = num_rx;
  num_tx_threads = num_tx;
  next_rx_id = 0;
  next_tx_id = 0;

  /* allocate thread pointer arrays */
  rx_threads = rte_calloc("rx thread ptrs", num_rx, sizeof(*rx_threads), 0);
  tx_threads = rte_calloc("tx thread ptrs", num_tx, sizeof(*tx_threads), 0);
  if (rx_threads == NULL || tx_threads == NULL) {
    goto error_exit;
  }

  /* make sure there is only one port */
  count = rte_eth_dev_count_avail();
  if (count == 0) {
    fprintf(stderr, "No ethernet devices\n");
    goto error_exit;
  } else if (count > 1) {
    fprintf(stderr, "Multiple ethernet devices\n");
    goto error_exit;
  }

  /* initialize port */
  ret = rte_eth_dev_configure(port_id, num_rx, num_tx, &port_conf);
  if (ret < 0) {
    fprintf(stderr, "rte_eth_dev_configure failed\n");
    goto error_exit;
  }

  /* get mac address and device info */
  rte_eth_macaddr_get(port_id, &eth_addr);
  rte_eth_dev_info_get(port_id, &eth_devinfo);
  //eth_devinfo.default_txconf.txq_flags = ETH_TXQ_FLAGS_NOVLANOFFL;


  return 0;

error_exit:
  rte_free(rx_threads);
  rte_free(tx_threads);
  return -1;
}

void network_cleanup(void)
{
  rte_eth_dev_stop(port_id);
  rte_free(rx_threads);
  rte_free(tx_threads);
}

void network_dump_stats(void)
{
  struct rte_eth_stats stats;
  if (rte_eth_stats_get(0, &stats) == 0) {
    fprintf(stderr, "network stats: ipackets=%"PRIu64" opackets=%"PRIu64
        " ibytes=%"PRIu64" obytes=%"PRIu64" imissed=%"PRIu64" ierrors=%"PRIu64
        " oerrors=%"PRIu64" rx_nombuf=%"PRIu64"\n", stats.ipackets,
        stats.opackets, stats.ibytes, stats.obytes, stats.imissed,
        stats.ierrors, stats.oerrors, stats.rx_nombuf);
  } else {
    fprintf(stderr, "failed to get stats\n");
  }
}

struct network_rx_thread *network_rx_thread_init(uint16_t id)
{
  struct network_rx_thread *t;
  int ret;

  /* allocate rx thread struct and mempool */
  if ((t = rte_zmalloc("rx thread struct", sizeof(*t), 0)) == NULL) {
    goto error_exit;
  }
  if ((t->pool = mempool_alloc()) == NULL) {
    goto error_mpool;
  }

  /* initialize queue */
  t->queue_id = id;
  assert(t->queue_id < num_rx_threads);
  ret = rte_eth_rx_queue_setup(port_id, t->queue_id, RX_DESCRIPTORS,
          rte_socket_id(), &eth_devinfo.default_rxconf, t->pool);
  if (ret != 0) {
    goto error_queue;
  }

  /* start device if this was the last queue */
  if (num_rx_threads == __sync_add_and_fetch(&next_rx_id, 1) &&
      num_tx_threads == next_tx_id)
  {
    if (rte_eth_dev_start(port_id) != 0) {
      fprintf(stderr, "rte_eth_dev_start failed\n");
      goto error_queue;
    }

    /* setting up RETA failed */
    if (reta_setup() != 0) {
      fprintf(stderr, "RETA setup failed\n");
      goto error_exit;
    }

    device_running = 1;
  }

#ifdef EMULATE_DROPS
  utils_rng_init(&t->drop_rng, (EMULATE_DROPS + 42) * t->queue_id +
      *(uint32_t *) &eth_addr);
#endif

  return t;

error_queue:
  /* TODO: free mempool */
error_mpool:
  rte_free(t);
error_exit:
  return NULL;
}

struct network_tx_thread *network_tx_thread_init(uint16_t id)
{
  struct network_tx_thread *t;
  int ret;

  /* allocate tx thread struct and mempool */
  if ((t = rte_zmalloc("tx thread struct", sizeof(*t), 0)) == NULL) {
    fprintf(stderr, "network_tx_thread_init: rte_zmalloc failed\n");
    goto error_exit;
  }
  if ((t->pool = mempool_alloc()) == NULL) {
    fprintf(stderr, "network_tx_thread_init: mempool_alloc failed\n");
    goto error_mpool;
  }

  /* initialize queue */
  t->queue_id = id;
  assert(t->queue_id < num_tx_threads);
  ret = rte_eth_tx_queue_setup(port_id, t->queue_id, TX_DESCRIPTORS,
          rte_socket_id(), &eth_devinfo.default_txconf);
  if (ret != 0) {
    fprintf(stderr, "network_tx_thread_init: rte_eth_tx_queue_setup failed\n");
    goto error_queue;
  }

  /* start device if this was the last queue */
  if (num_rx_threads == __sync_add_and_fetch(&next_tx_id, 1) &&
      num_tx_threads == next_tx_id)
  {
    if (rte_eth_dev_start(port_id) != 0) {
      fprintf(stderr, "rte_eth_dev_start failed\n");
      goto error_queue;
    }

    /* setting up RETA failed */
    if (reta_setup() != 0) {
      fprintf(stderr, "RETA setup failed\n");
      goto error_exit;
    }

    device_running = 1;
  }

  return t;

error_queue:
  /* TODO: free mempool */
error_mpool:
  rte_free(t);
error_exit:
  return NULL;
}

int network_poll(struct network_rx_thread *t, unsigned num, uint16_t *offs,
        uint16_t *lens, void **bufs, struct network_buf_handle **bhs)
{
  struct rte_mbuf **mbs = (struct rte_mbuf **) bhs;
  unsigned i;

  num = rte_eth_rx_burst(port_id, t->queue_id, mbs, num);
  if (num == 0) {
    return 0;
  }

#ifdef EMULATE_DROPS
  unsigned j;
  for (i = 0, j = 0; i < num; i++) {
    if (utils_rng_gen32(&t->drop_rng) % EMULATE_DROPS == 0) {
      rte_pktmbuf_free_seg(mbs[i]);
    } else {
      mbs[j++] = mbs[i];
    }
  }
  num = j;
#endif

  for (i = 0; i < num; i++) {
    offs[i] = mbs[i]->data_off;
    assert(mbs[i]->pkt_len == mbs[i]->data_len);
    lens[i] = mbs[i]->data_len;
    bufs[i] = mbs[i]->buf_addr;

#ifdef FLEXNIC_TRACE_TX
    trace_event(FLEXNIC_TRACE_EV_RXPKT, lens[i], (uint8_t *) bufs[i] + offs[i]);
#endif
  }

  return num;
}

int network_rx_interrupt_ctl(struct network_rx_thread *t, int turnon)
{
  static int __thread initialized = 0;

  if(!device_running) {
    return 1;
  }

  if(turnon) {
    if(!initialized) {
      int ret = rte_eth_dev_rx_intr_ctl_q(port_id, t->queue_id, RTE_EPOLL_PER_THREAD,
					  RTE_INTR_EVENT_ADD, NULL);
      assert(ret == 0);
      initialized = 1;
    }

    return rte_eth_dev_rx_intr_enable(port_id, t->queue_id);
  } else {
    return rte_eth_dev_rx_intr_disable(port_id, t->queue_id);
  }
}

int network_send(struct network_tx_thread *t, unsigned num, uint16_t *offs,
        uint16_t *lens, struct network_buf_handle **bhs)
{
  struct rte_mbuf **mbs = (struct rte_mbuf **) bhs;
  unsigned i;

  for (i = 0; i < num; i++) {
    mbs[i]->data_off = offs[i];
    mbs[i]->pkt_len = mbs[i]->data_len = lens[i];

#ifdef FLEXNIC_TRACE_TX
    trace_event(FLEXNIC_TRACE_EV_TXPKT, lens[i],
        (uint8_t *) mbs[i]->buf_addr + offs[i]);
#endif
  }

  return rte_eth_tx_burst(port_id, t->queue_id, mbs, num);
}

int network_buf_alloc(struct network_tx_thread *t, unsigned num, void **bufs,
        struct network_buf_handle **bhs)
{
  struct rte_mbuf **mbs = (struct rte_mbuf **) bhs;
  unsigned i;

#if 0 /* for newer dpdk version */
  if (rte_pktmbuf_alloc_bulk(t->pool, mbs, num) != 0) {
    return -1;
  }
#endif

  for (i = 0; i < num; i++) {
    if ((mbs[i] = rte_pktmbuf_alloc(t->pool)) == NULL) {
      break;
    }
    bufs[i] = mbs[i]->buf_addr;
  }

  return i;
}

void network_free(unsigned num, struct network_buf_handle **bufs)
{
  unsigned i;
  for (i = 0; i < num; i++) {
    rte_pktmbuf_free_seg((struct rte_mbuf *) bufs[i]);
  }
}

static struct rte_mempool *mempool_alloc(void)
{
  static unsigned pool_id = 0;
  unsigned n;
  char name[32];
  n = __sync_fetch_and_add(&pool_id, 1);
  snprintf(name, 32, "mbuf_pool_%u\n", n);
  return rte_mempool_create(name, PERTHREAD_MBUFS, MBUF_SIZE, 32,
          sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
          rte_pktmbuf_init, NULL, rte_socket_id(), 0);

}

void network_buf_reset(struct network_buf_handle *bh)
{
  struct rte_mbuf *mb = (struct rte_mbuf *) bh;
  mb->ol_flags = 0;
}

uint16_t network_buf_tcpxsums(struct network_buf_handle *bh, uint8_t l2l,
    uint8_t l3l, void *ip_hdr)
{
  struct rte_mbuf *mb = (struct rte_mbuf *) bh;
  mb->l2_len = l2l;
  mb->l3_len = l3l;
  mb->ol_flags = PKT_TX_IPV4 | PKT_TX_IP_CKSUM | PKT_TX_TCP_CKSUM;
  return rte_ipv4_phdr_cksum(ip_hdr, PKT_TX_IPV4 | PKT_TX_IP_CKSUM |
      PKT_TX_TCP_CKSUM);
}

static inline uint16_t core_min(uint16_t num)
{
  uint16_t i, i_min = 0, v_min = UINT8_MAX;

  for (i = 0; i < num; i++) {
    if (rss_core_buckets[i] < v_min) {
      v_min = rss_core_buckets[i];
      i_min = i;
    }
  }

  return i_min;
}

static inline uint16_t core_max(uint16_t num)
{
  uint16_t i, i_max = 0, v_max = 0;

  for (i = 0; i < num; i++) {
    if (rss_core_buckets[i] >= v_max) {
      v_max = rss_core_buckets[i];
      i_max = i;
    }
  }

  return i_max;
}

int network_scale_up(uint16_t old, uint16_t new)
{
  uint16_t i, j, k, c, share = rss_reta_size / new;
  uint16_t outer, inner;

  /* clear mask */
  for (k = 0; k < rss_reta_size; k += RTE_RETA_GROUP_SIZE) {
    rss_reta[k / RTE_RETA_GROUP_SIZE].mask = 0;
  }

  k = 0;
  for (j = old; j < new; j++) {
    for (i = 0; i < share; i++) {
      c = core_max(old);

      for (; ; k = (k + 1) % rss_reta_size) {
        outer = k / RTE_RETA_GROUP_SIZE;
        inner = k % RTE_RETA_GROUP_SIZE;
        if (rss_reta[outer].reta[inner] == c) {
          rss_reta[outer].mask |= 1ULL << inner;
          rss_reta[outer].reta[inner] = j;
          pl_memory->flow_group_steering[k] = j;
          break;
        }
      }

      rss_core_buckets[c]--;
      rss_core_buckets[j]++;
    }
  }

  if (rte_eth_dev_rss_reta_update(port_id, rss_reta, rss_reta_size) != 0) {
    fprintf(stderr, "network_scale_up: rte_eth_dev_rss_reta_update failed\n");
    return -1;
  }

  return 0;
}

int network_scale_down(uint16_t old, uint16_t new)
{
  uint16_t i, o_c, n_c, outer, inner;

  /* clear mask */
  for (i = 0; i < rss_reta_size; i += RTE_RETA_GROUP_SIZE) {
    rss_reta[i / RTE_RETA_GROUP_SIZE].mask = 0;
  }

  for (i = 0; i < rss_reta_size; i++) {
    outer = i / RTE_RETA_GROUP_SIZE;
    inner = i % RTE_RETA_GROUP_SIZE;

    o_c = rss_reta[outer].reta[inner];
    if (o_c >= new) {
      n_c = core_min(new);

      rss_reta[outer].reta[inner] = n_c;
      rss_reta[outer].mask |= 1ULL << inner;

      pl_memory->flow_group_steering[i] = n_c;

      rss_core_buckets[o_c]--;
      rss_core_buckets[n_c]++;
    }
  }

  if (rte_eth_dev_rss_reta_update(port_id, rss_reta, rss_reta_size) != 0) {
    fprintf(stderr, "network_scale_down: rte_eth_dev_rss_reta_update failed\n");
    return -1;
  }

  return 0;
}

static int reta_setup()
{
  uint16_t i, c;

  /* allocate RSS redirection table and core-bucket count table */
  rss_reta_size = eth_devinfo.reta_size;
  rss_reta = rte_calloc("rss reta", (rss_reta_size / RTE_RETA_GROUP_SIZE),
      sizeof(*rss_reta), 0);
  rss_core_buckets = rte_calloc("rss core buckets", fp_cores_max,
      sizeof(*rss_core_buckets), 0);

  if (rss_reta == NULL || rss_core_buckets == NULL) {
    fprintf(stderr, "reta_setup: rss_reta alloc failed\n");
    goto error_exit;
  }

  if (rss_reta_size > FLEXNIC_PL_MAX_FLOWGROUPS) {
    fprintf(stderr, "reta_setup: reta size (%u) greater than maximum supported"
        " (%u)\n", rss_reta_size, FLEXNIC_PL_MAX_FLOWGROUPS);
    abort();
  }

  /* initialize reta */
  for (i = 0, c = 0; i < rss_reta_size; i++) {
    rss_core_buckets[c]++;
    rss_reta[i / RTE_RETA_GROUP_SIZE].mask = -1ULL;
    rss_reta[i / RTE_RETA_GROUP_SIZE].reta[i % RTE_RETA_GROUP_SIZE] = c;
    pl_memory->flow_group_steering[i] = c;
    c = (c + 1) % fp_cores_cur;
  }

  if (rte_eth_dev_rss_reta_update(port_id, rss_reta, rss_reta_size) != 0) {
    fprintf(stderr, "reta_setup: rte_eth_dev_rss_reta_update failed\n");
    return -1;
  }

  return 0;

error_exit:
  rte_free(rss_core_buckets);
  rte_free(rss_reta);
  return -1;
}

int network_buf_flowgroup(struct network_buf_handle *bh, uint16_t *fg)
{
  struct rte_mbuf *mb = (struct rte_mbuf *) bh;
  if (!(mb->ol_flags & PKT_RX_RSS_HASH)) {
    *fg = 0;
    return 0;
  }

  *fg = mb->hash.rss & (rss_reta_size - 1);
  return 0;
}
