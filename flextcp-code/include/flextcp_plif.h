#ifndef FLEXTCP_PLIF_H_
#define FLEXTCP_PLIF_H_

#include <stdint.h>
#include <flexnic.h>
#include <utils.h>
#include <packet_defs.h>

/******************************************************************************/
/* Kernel doorbells */
#define FLEXTCP_PL_KDB_BUMPQUEUE 0x2

/** Kernel doorbell format */
struct flextcp_pl_kdb {
  union {
    struct {
      uint32_t rx_tail;
      uint32_t tx_tail;
    } bumpqueue;
  } msg;
  uint8_t _pad[52];
  uint16_t flags;
  uint8_t _pad1;
  volatile uint8_t nic_own;
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct flextcp_pl_kdb) == FLEXNIC_DB_BYTES, kdb_size);


/******************************************************************************/
/* Kernel RX queue */

#define FLEXTCP_PL_KRX_INVALID 0x0
#define FLEXTCP_PL_KRX_PACKET 0x1

/** Kernel RX queue entry */
struct flextcp_pl_krx {
  uint64_t addr;
  union {
    struct {
      uint16_t len;
      uint16_t fn_core;
      uint16_t flow_group;
    } packet;
    uint8_t raw[63];
  } __attribute__((packed)) msg;
  volatile uint8_t type;
} __attribute__((packed));


/******************************************************************************/
/* Kernel TX queue */

#define FLEXTCP_PL_KTX_INVALID 0x0
#define FLEXTCP_PL_KTX_PACKET 0x1
#define FLEXTCP_PL_KTX_CONNRETRAN 0x2

/** Kernel TX queue entry */
struct flextcp_pl_ktx {
  union {
    struct {
      uint64_t addr;
      uint16_t len;
    } packet;
    struct {
      uint32_t flow_id;
    } connretran;
    uint8_t raw[63];
  } __attribute__((packed)) msg;
  volatile uint8_t type;
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct flextcp_pl_ktx) == 64, ktx_size);


/******************************************************************************/
/* App doorbell */

#define FLEXTCP_PL_ADB_BUMPQUEUE 0x0

/** Application doorbell format */
struct flextcp_pl_adb {
  union {
    struct {
      uint32_t rx_tail;
      uint32_t tx_tail;
    } bumpqueue;
    uint8_t raw[62];
  } __attribute__((packed)) msg;
  uint8_t type;
  volatile uint8_t nic_own;
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct flextcp_pl_adb) == FLEXNIC_DB_BYTES, adb_size);

/******************************************************************************/
/* App RX queue */

#define FLEXTCP_PL_ARX_INVALID    0x0
#define FLEXTCP_PL_ARX_CONNUPDATE 0x1
#define FLEXTCP_PL_ARX_OBJUPDATE  0x2
#define FLEXTCP_PL_ARX_DUMMY      0x3

/** Update receive and transmit buffer of flow */
struct flextcp_pl_arx_connupdate {
  uint64_t opaque;
  uint32_t rx_bump;
  uint32_t rx_pos;
  uint32_t tx_bump;
} __attribute__((packed));

/** Application RX queue entry */
struct flextcp_pl_arx {
  union {
    struct flextcp_pl_arx_connupdate connupdate;
    uint8_t raw[59];
  } __attribute__((packed)) msg;
  uint32_t tx_head;
  volatile uint8_t type;
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct flextcp_pl_arx) == 64, arx_size);

/******************************************************************************/
/* App TX queue */

#define FLEXTCP_PL_ATX_CONNUPDATE 0x1

/** Application TX queue entry */
struct flextcp_pl_atx {
  union {
    struct {
      uint32_t rx_tail;
      uint32_t tx_head;
      uint32_t flow_id;
      uint32_t bump_seq;
    } connupdate;
    uint8_t raw[63];
  } __attribute__((packed)) msg;
  volatile uint8_t type;
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct flextcp_pl_atx) == 64, atx_size);

/******************************************************************************/
/* Internal flexnic memory */

#define FLEXNIC_PL_APPST_NUM        8
#define FLEXNIC_PL_APPST_CTX_NUM   31
#define FLEXNIC_PL_APPST_CTX_MCS    8
#define FLEXNIC_PL_APPCTX_NUM      16
#define FLEXNIC_PL_FLOWST_NUM     (128 * 1024)
#define FLEXNIC_PL_FLOWHT_ENTRIES (FLEXNIC_PL_FLOWST_NUM * 2)
#define FLEXNIC_PL_FLOWHT_NBSZ      4
#define FLEXNIC_PL_QMANTBL_NUM   (2 * FLEXNIC_PL_FLOWST_NUM)

/** Application state */
struct flextcp_pl_appst {
  /********************************************************/
  /* read-only fields */

  /** Number of contexts */
  uint16_t ctx_num;

  /** IDs of contexts */
  uint16_t ctx_ids[FLEXNIC_PL_APPST_CTX_NUM];
} __attribute__((packed));


/** Application context registers */
struct flextcp_pl_appctx {
  /********************************************************/
  /* read-only fields */
  uint64_t rx_base;
  uint64_t tx_base;
  uint32_t rx_len;
  uint32_t tx_len;
  uint32_t qman_qid;
  uint32_t appst_id;
  int	   evfd;

  /********************************************************/
  /* read-write fields */
  uint32_t rx_head;
  uint32_t tx_head;
  uint32_t last_ts;
} __attribute__((packed));

/** Enable out of order receive processing members */
#define FLEXNIC_PL_OOO_RECV 1

#define FLEXNIC_PL_FLOWST_SLOWPATH 1
#define FLEXNIC_PL_FLOWST_OBJCONN 2
#define FLEXNIC_PL_FLOWST_OBJNOHASH 4
#define FLEXNIC_PL_FLOWST_ECN 8
#define FLEXNIC_PL_FLOWST_RX_MASK (~15ULL)

/** Flow state registers */
struct flextcp_pl_flowst {
  /********************************************************/
  /* read-only fields */

  /** Opaque flow identifier from application */
  uint64_t opaque;

  /** Base address of receive buffer */
  uint64_t rx_base_sp;
  /** Base address of transmit buffer */
  uint64_t tx_base;

  /** Length of receive buffer */
  uint32_t rx_len;
  /** Length of transmit buffer */
  uint32_t tx_len;

  /** Queue manager queue id */
  uint32_t qman_qid;

  beui32_t local_ip;
  beui32_t remote_ip;

  beui16_t local_port;
  beui16_t remote_port;

  /** Remote MAC address */
  struct eth_addr remote_mac;

  /** Doorbell ID (identifying the app ctx to use) */
  uint16_t db_id;

  /** Flow group for this connection (rss bucket) */
  uint32_t flow_group;


  /********************************************************/
  /* read-write fields */

  /** spin lock */
  volatile uint32_t lock;

  /** Bytes available for received segments at next position */
  uint32_t rx_avail;
  /** Offset in buffer to place next segment */
  uint32_t rx_next_pos;
  /** Next sequence number expected */
  uint32_t rx_next_seq;
  /** Bytes available in remote end for received segments */
  uint32_t rx_remote_avail;
  /** Bytes left in current object */
  uint32_t rx_objrem;
  /** Duplicate ack count */
  uint32_t rx_dupack_cnt;

#ifdef FLEXNIC_PL_OOO_RECV
  /* Start of interval of out-of-order received data */
  uint32_t rx_ooo_start;
  /* Length of interval of out-of-order received data */
  uint32_t rx_ooo_len;
#endif

  /** Number of bytes up to next pos in the buffer that were sent but not
   * acknowledged yet. */
  uint32_t tx_sent;
  /** Offset in buffer for next segment to be sent */
  uint32_t tx_next_pos;
  /** Sequence number of next segment to be sent */
  uint32_t tx_next_seq;
  /** End of data that is ready to be sent */
  uint32_t tx_head;
  /** Bytes left in current object */
  uint32_t tx_objrem;
  /** Timestamp to echo in next packet */
  uint32_t tx_next_ts;

  /** Sequence number of queue pointer bumps */
  uint32_t bump_seq;

  /** Congestion control rate [kbps] */
  uint32_t tx_rate;
  /** Counter drops */
  uint32_t cnt_tx_drops;
  /** Counter acks */
  uint32_t cnt_rx_acks;
  /** Counter bytes sent */
  uint32_t cnt_rx_ack_bytes;
  /** Counter acks marked */
  uint32_t cnt_rx_ecn;
  /** RTT estimate */
  uint32_t rtt_est;
} __attribute__((packed, aligned(64)));

#define FLEXNIC_PL_FLOWHTE_VALID  (1 << 31)
#define FLEXNIC_PL_FLOWHTE_POSSHIFT 29

/** Flow lookup table entry */
struct flextcp_pl_flowhte {
  uint32_t flow_id;
  uint32_t flow_hash;
} __attribute__((packed));

#define FLEXNIC_PL_QMANTE_TYPEMASK (3 << 30)
#define FLEXNIC_PL_QMANTE_TYPEACTX (0 << 30)
#define FLEXNIC_PL_QMANTE_TYPEFLOW (1 << 30)

/** Queue manager queue table entry */
struct flextcp_pl_qmante {
  uint32_t id;
} __attribute__((packed));

#define FLEXNIC_PL_MAX_FLOWGROUPS 4096

/** Layout of internal pipeline memory */
struct flextcp_pl_mem {
  /* registers for application context queues */
  struct flextcp_pl_appctx appctx[FLEXNIC_PL_APPST_CTX_MCS][FLEXNIC_PL_APPCTX_NUM];

  /* registers for flow state */
  struct flextcp_pl_flowst flowst[FLEXNIC_PL_FLOWST_NUM];

  /* flow lookup table */
  struct flextcp_pl_flowhte flowht[FLEXNIC_PL_FLOWHT_ENTRIES];

  /* queue manager queue table */
  struct flextcp_pl_qmante qmant[FLEXNIC_PL_QMANTBL_NUM];

  /* registers for kernel queues */
  struct flextcp_pl_appctx kctx[FLEXNIC_PL_APPST_CTX_MCS];

  /* registers for application state */
  struct flextcp_pl_appst appst[FLEXNIC_PL_APPST_NUM];

  uint8_t flow_group_steering[FLEXNIC_PL_MAX_FLOWGROUPS];
} __attribute__((packed));


/******************************************************************************/
/* Pipeline trace events */

#define FLEXNIC_PL_TREV_ADB       0x100
#define FLEXNIC_PL_TREV_ATX       0x101
#define FLEXNIC_PL_TREV_ARX       0x102
#define FLEXNIC_PL_TREV_RXFS      0x103
#define FLEXNIC_PL_TREV_TXACK     0x104
#define FLEXNIC_PL_TREV_TXSEG     0x105
#define FLEXNIC_PL_TREV_ACTXQMAN  0x106
#define FLEXNIC_PL_TREV_AFLOQMAN  0x107
#define FLEXNIC_PL_TREV_APARSEO   0x108

/** application queue bump */
struct flextcp_pl_trev_adb {
  uint32_t rx_tail;
  uint32_t tx_tail;
  uint32_t rx_tail_prev;
  uint32_t tx_tail_prev;
  uint32_t rx_head;
  uint32_t tx_head;

  uint32_t db_id;
  uint32_t qman_qid;
} __attribute__((packed));

/** application tx queue entry */
struct flextcp_pl_trev_atx {
  uint32_t rx_tail;
  uint32_t tx_head;
  uint32_t bump_seq_ent;

  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;

  uint32_t flow_id;
  uint32_t db_id;

  uint32_t tx_next_pos;
  uint32_t tx_next_seq;
  uint32_t tx_head_prev;
  uint32_t rx_next_pos;
  uint32_t rx_avail;
  uint32_t tx_len;
  uint32_t rx_len;
  uint32_t rx_remote_avail;
  uint32_t tx_sent;
  uint32_t bump_seq_flow;
} __attribute__((packed));

/** application rx queue entry */
struct flextcp_pl_trev_arx {
  uint32_t rx_bump;
  uint32_t tx_bump;

  uint32_t db_id;

  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;
} __attribute__((packed));

/** tcp flow state on receive */
struct flextcp_pl_trev_rxfs {
  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;

  uint32_t flow_seq;
  uint32_t flow_ack;
  uint16_t flow_flags;
  uint16_t flow_len;

  uint32_t fs_rx_nextpos;
  uint32_t fs_rx_nextseq;
  uint32_t fs_rx_avail;
  uint32_t fs_tx_nextpos;
  uint32_t fs_tx_nextseq;
  uint32_t fs_tx_sent;
} __attribute__((packed));

/** tcp ack sent out */
struct flextcp_pl_trev_txack {
  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;

  uint32_t flow_seq;
  uint32_t flow_ack;
  uint16_t flow_flags;
} __attribute__((packed));

/* tcp segment sent out */
struct flextcp_pl_trev_txseg {
  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;

  uint32_t flow_seq;
  uint32_t flow_ack;
  uint16_t flow_flags;
  uint16_t flow_len;
} __attribute__((packed));

/* queue manager event fetching tx queue entry */
struct flextcp_pl_trev_actxqman {
  uint64_t tx_base;
  uint32_t tx_len;
  uint32_t tx_head;
  uint32_t tx_head_last;
  uint32_t tx_tail;

  uint32_t db_id;
} __attribute__((packed));

/* queue manager event fetching flow payload */
struct flextcp_pl_trev_afloqman {
  uint64_t tx_base;
  uint32_t tx_head;
  uint32_t tx_next_pos;
  uint32_t tx_len;
  uint32_t rx_remote_avail;
  uint32_t tx_sent;
  uint32_t tx_objrem;

  uint32_t flow_id;
} __attribute__((packed));

/* parse object headers */
struct flextcp_pl_trev_aparseobj {
  uint64_t tx_base;
  uint32_t tx_head;
  uint32_t tx_next_pos;
  uint32_t tx_next_seq;
  uint32_t tx_len;
  uint32_t rx_remote_avail;
  uint32_t rx_avail;
  uint32_t tx_sent;
  uint32_t tx_objrem;

  uint32_t rem_len;
  uint32_t objlen;

  uint32_t flow_id;
} __attribute__((packed));

void util_flexnic_kick(struct flextcp_pl_appctx *ctx);

#endif /* ndef FLEXTCP_PLIF_H_ */
