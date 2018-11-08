#ifndef FLEXNIC_TRACE_H_
#define FLEXNIC_TRACE_H_

#include <stdint.h>

#define FLEXNIC_TRACE_NAME "flexnic_trace_%u"

#define FLEXNIC_TRACE_EV_RXPKT 1
#define FLEXNIC_TRACE_EV_TXPKT 2
#define FLEXNIC_TRACE_EV_DMARD 3
#define FLEXNIC_TRACE_EV_DMAWR 4
#define FLEXNIC_TRACE_EV_PCIDB 5
#define FLEXNIC_TRACE_EV_QMSET 6
#define FLEXNIC_TRACE_EV_QMEVT 7

struct flexnic_trace_header {
  volatile uint64_t end_last;
  uint64_t length;
} __attribute__((packed));

struct flexnic_trace_entry_head {
  uint64_t ts;
  uint32_t seq;
  uint16_t type;
  uint16_t length;
} __attribute__((packed));

struct flexnic_trace_entry_tail {
  uint16_t length;
} __attribute__((packed));

struct flexnic_trace_entry_dma {
  uint64_t addr;
  uint64_t len;
  uint8_t data[];
} __attribute__((packed));

struct flexnic_trace_entry_pcidb {
  uint64_t id;
  uint8_t data[];
} __attribute__((packed));

struct flexnic_trace_entry_qman_set {
  uint32_t id;
  uint32_t rate;
  uint32_t avail;
  uint16_t max_chunk;
  uint8_t  opaque;
  uint8_t  flags;
} __attribute__((packed));

struct flexnic_trace_entry_qman_event {
  uint32_t id;
  uint16_t bytes;
  uint8_t  opaque;
  uint8_t  pad;
} __attribute__((packed));

#endif
