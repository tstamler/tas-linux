#ifndef FLEXNIC_H_
#define FLEXNIC_H_
#include <stdint.h>

#define FLEXNIC_USE_HUGEPAGES 1
#define FLEXNIC_HUGE_PREFIX "/mnt/huge"

/** Name for the info shared memory region. */
#define FLEXNIC_NAME_INFO "flexnic_info"
/** Name for flexnic dma shared memory region. */
#define FLEXNIC_NAME_DMA_MEM "flexnic_memory"
/** Name for flexnic internal shared memory region. */
#define FLEXNIC_NAME_INTERNAL_MEM "flexnic_internal"
/** Name template (printf) for flexnic doorbell queues, takes int db id. */
#define FLEXNIC_NAME_DBS "flexnic_doobell_%03d"

/** Size of entries in doorbell queue. */
#define FLEXNIC_DB_BYTES 64

/** Size of the info shared memory region. */
#define FLEXNIC_INFO_BYTES 0x1000

/** Indicates that flexnic is done initializing. */
#define FLEXNIC_FLAG_READY 1

/** Info struct: layout of info shared memory region */
struct flexnic_info {
  /** Flags: see FLEXNIC_FLAG_* */
  uint64_t flags;
  /** Size of flexnic dma memory in bytes. */
  uint64_t dma_mem_size;
  /** Size of internal flexnic memory in bytes. */
  uint64_t internal_mem_size;
  /** Number of doorbell queues. */
  uint32_t db_num;
  /** Number of entries in each doorbell queue. */
  uint32_t db_qlen;
  /** Number of queues in queue manager */
  uint32_t qmq_num;
  /** Number of cores in flexnic emulator */
  uint32_t cores_num;
  /** MAC address */
  uint64_t mac_addr;
} __attribute__((packed));

#endif /* ndef FLEXNIC_H_ */
