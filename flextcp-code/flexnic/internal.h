#ifndef INTERNAL_H_
#define INTERNAL_H_

#include <stddef.h>
#include <stdint.h>

#include <rte_config.h>
#include <rte_ether.h>

#include <flexnic.h>

#define BUFFER_SIZE 2048
#define FLEXNIC_NUM_DOORBELL 32
#define FLEXNIC_DB_QLEN 1024
#define FLEXNIC_DMA_MEM_SIZE (1024 * 1024 * 1024)
#define FLEXNIC_INTERNAL_MEM_SIZE (1024 * 1024 * 32)
#define FLEXNIC_NUM_QMQUEUES (128 * 1024)

//#define FLEXNIC_TRACING
#ifdef FLEXNIC_TRACING
#   include <flexnic_trace.h>
#   define FLEXNIC_TRACE_RX
#   define FLEXNIC_TRACE_TX
#   define FLEXNIC_TRACE_DMA
#   define FLEXNIC_TRACE_QMAN
#   define FLEXNIC_TRACE_LEN (1024 * 1024 * 32)
#endif
//#define DATAPLANE_STATS

struct network_rx_thread;
struct network_tx_thread;
struct network_buf_handle;
struct dataplane_context;

extern struct ether_addr eth_addr;
extern int exited;
extern unsigned fp_cores_max;
extern volatile unsigned fp_cores_cur;
extern volatile unsigned fp_scale_to;
extern struct flextcp_pl_mem *pl_memory;


int dma_preinit(void);
int dma_init(unsigned threads);
void dma_cleanup(void);
void dma_set_ready(void);

int dma_read(uintptr_t addr, size_t len, void *buf);
int dma_write(uintptr_t addr, size_t len, const void *buf);
int dma_pointer(uintptr_t addr, size_t len, void **buf);
#ifdef DATAPLANE_STATS
void dma_dump_stats(void);
#endif


int network_init(unsigned rx_threads, unsigned tx_threads);
void network_cleanup(void);

struct network_rx_thread *network_rx_thread_init(uint16_t id);
struct network_tx_thread *network_tx_thread_init(uint16_t id);

int network_poll(struct network_rx_thread *t, unsigned num, uint16_t *offs,
        uint16_t *lens, void **bufs, struct network_buf_handle **bhs);
int network_send(struct network_tx_thread *t, unsigned num, uint16_t *offs,
        uint16_t *lens, struct network_buf_handle **bhs);

int network_rx_interrupt_ctl(struct network_rx_thread *t, int turnon);

void network_buf_reset(struct network_buf_handle *bh);
uint16_t network_buf_tcpxsums(struct network_buf_handle *bh, uint8_t l2l,
    uint8_t l3l, void *ip_hdr);
int network_buf_flowgroup(struct network_buf_handle *bh, uint16_t *fg);

int network_buf_alloc(struct network_tx_thread *t, unsigned num, void **bufs,
        struct network_buf_handle **bhs);
void network_free(unsigned num, struct network_buf_handle **bufs);

int network_scale_up(uint16_t old, uint16_t new);
int network_scale_down(uint16_t old, uint16_t new);

#define QMAN_SET_RATE     (1 << 0)
#define QMAN_SET_MAXCHUNK (1 << 1)
#define QMAN_SET_OPAQUE   (1 << 2)
#define QMAN_SET_AVAIL    (1 << 3)
#define QMAN_ADD_AVAIL    (1 << 4)

int qman_init(unsigned threads);
struct qman_thread *qman_thread_init(uint16_t id);
uint32_t qman_timestamp(uint64_t tsc);
int qman_poll(struct qman_thread *t, unsigned num, unsigned *q_ids,
    uint16_t *q_bytes, uint8_t *q_opaque);
int qman_set(struct qman_thread *t, uint32_t id, uint32_t rate, uint32_t avail,
    uint16_t max_chunk, uint8_t opaque, uint8_t flags);
uint32_t qman_next_ts(struct qman_thread *t, uint32_t cur_ts);

int dataplane_init(unsigned threads);
struct dataplane_context *dataplane_context_init(uint16_t id,
    struct network_rx_thread *rx_t, struct network_tx_thread *tx_t,
    struct qman_thread *qman_t);
void dataplane_context_destroy(struct dataplane_context *ctx);
void dataplane_loop(struct dataplane_context *ctx);
#ifdef DATAPLANE_STATS
void dataplane_dump_stats(void);
#endif

#ifdef FLEXNIC_TRACING
int trace_thread_init(uint16_t id);
int trace_event(uint16_t type, uint16_t length, const void *buf);
int trace_event2(uint16_t type, uint16_t len_1, const void *buf_1,
    uint16_t len_2, const void *buf_2);
#endif

#endif /* ndef INTERNAL_H_ */
