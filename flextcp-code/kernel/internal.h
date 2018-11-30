#ifndef INTERNAL_H_
#define INTERNAL_H_

/** @addtogroup kernel
 *  @brief Kernel */

#include <stdint.h>

#include <flexnic_driver.h>
#include <utils_nbqueue.h>
#include <utils_timeout.h>
#include "tap.h"

struct configuration;
struct config_route;
struct connection;
struct kernel_statistics;
struct listener;
struct timeout;
enum timeout_type;

extern void *packetmem;
extern struct timeout_manager timeout_mgr;
extern struct configuration config;
extern struct kernel_statistics kstats;
extern uint32_t cur_ts;
extern int kernel_notifyfd;

struct nicif_completion {
  struct nbqueue_el el;
  struct nbqueue *q;
  int notify_fd;
  int32_t status;
  void *ptr;
};

struct kernel_statistics {
  /** drops detected by flextcp on NIC */
  uint64_t drops;
  /** kernel re-transmission timeouts */
  uint64_t kernel_rexmit;
  /** # of ECN marked ACKs */
  uint64_t ecn_marked;
  /** total number of ACKs */
  uint64_t acks;
};

/** Type of timeout */
enum timeout_type {
  /** ARP request */
  TO_ARP_REQ,
  /** TCP handshake sent */
  TO_TCP_HANDSHAKE,
  /** TCP retransmission timeout */
  TO_TCP_RETRANSMIT,
};

/*****************************************************************************/
/**
 * @addtogroup kernel-config
 * @brief Configuration management
 * @ingroup kernel
 * @{ */

/** Supported congestion control algorithms. */
enum config_cc_algorithm {
  /** Window-based DCTCP */
  CONFIG_CC_DCTCP_WIN,
  /** Rate-based DCTCP */
  CONFIG_CC_DCTCP_RATE,
  /** TIMELY */
  CONFIG_CC_TIMELY,
  /** Constant connection rate */
  CONFIG_CC_CONST_RATE,
};

/** Struct containing the parsed configuration parameters */
struct configuration {
  /** Kernel nic receive queue length. */
  uint64_t nic_rx_len;
  /** Kernel nic transmit queue length. */
  uint64_t nic_tx_len;
  /** App context -> kernel queue length. */
  uint64_t app_kin_len;
  /** App context <- kernel queue length. */
  uint64_t app_kout_len;
  /** TCP receive buffer size. */
  uint64_t tcp_rxbuf_len;
  /** TCP transmit buffer size. */
  uint64_t tcp_txbuf_len;
  /** Initial tcp rtt for cc rate [us]*/
  uint32_t tcp_rtt_init;
  /** Link bandwidth for converting window to rate [gbps] */
  uint32_t tcp_link_bw;
  /** Initial tcp handshake timeout [us] */
  uint32_t tcp_handshake_to;
  /** # of retries for dropped handshake packets */
  uint32_t tcp_handshake_retries;
  /** IP address for this host */
  uint32_t ip;
  /** IP prefix length for this host */
  uint8_t ip_prefix;
  /** List of routes */
  struct config_route *routes;
  /** Initial ARP timeout in [us] */
  uint32_t arp_to;
  /** Maximum ARP timeout [us] */
  uint32_t arp_to_max;
  /** Congestion control algorithm */
  enum config_cc_algorithm cc_algorithm;
  /** CC: minimum delay between running control loop [us] */
  uint32_t cc_control_granularity;
  /** CC: control interval (multiples of conn RTT) */
  uint32_t cc_control_interval;
  /** CC: number of intervals without ACKs before retransmit */
  uint32_t cc_rexmit_ints;
  /** CC dctcp: EWMA weight for new ECN */
  uint32_t cc_dctcp_weight;
  /** CC dctcp: initial rate [kbps] */
  uint32_t cc_dctcp_init;
  /** CC dctcp: additive increase step [kbps] */
  uint32_t cc_dctcp_step;
  /** CC dctcp: multiplicative increase */
  uint32_t cc_dctcp_mimd;
  /** CC dctcp: minimal rate */
  uint32_t cc_dctcp_min;
  /** CC dctcp: min number of packets to wait for */
  uint32_t cc_dctcp_minpkts;
  /** CC Const: Rate to assign to flows [kbps] */
  uint32_t cc_const_rate;
  /** CC timely: low threshold */
  uint32_t cc_timely_tlow;
  /** CC timely: high threshold */
  uint32_t cc_timely_thigh;
  /** CC timely: additive increment step [kbps] */
  uint32_t cc_timely_step;
  /** CC timely: initial rate [kbps] */
  uint32_t cc_timely_init;
  /** CC timely: ewma weight for rtt diff */
  uint32_t cc_timely_alpha;
  /** CC timely: multiplicative decrement factor */
  uint32_t cc_timely_beta;
  /** CC timely: minimal RTT without queuing */
  uint32_t cc_timely_min_rtt;
  /** CC timely: minimal rate to use */
  uint32_t cc_timely_min_rate;
};

/** Route entry in configuration */
struct config_route {
  /** Destination IP address */
  uint32_t ip;
  /** Destination prefix length */
  uint8_t ip_prefix;
  /** Next hop IP */
  uint32_t next_hop_ip;
  /** Next pointer for route list */
  struct config_route *next;
};

/**
 * Parse command line parameters to fill in configuration struct.
 *
 * @param c    Config struct to store parameters in.
 * @param argc Argument count.
 * @param argv Argument vector.
 *
 * @return 0 on success, != 0.
 */
int config_parse(struct configuration *c, int argc, char *argv[]);

/** @} */

/*****************************************************************************/
/**
 * @addtogroup kernel-nicif
 * @brief NIC Interface
 * @ingroup kernel
 * @{ */

extern struct flexnic_info *flexnic_info;

/** Initialize NIC interface */
int nicif_init(void);

/** Poll NIC queues */
unsigned nicif_poll(void);

/**
 * Register application context (must be called from poll thread).
 *
 * @param appid    Application ID
 * @param db       Doorbell ID
 * @param rxq_base Base addresses of context receive queue
 * @param rxq_len  Length of context receive queue
 * @param txq_base Base addresses of context transmit queue
 * @param txq_len  Length of context transmit queue
 * @param evfd     Event FD used to ping app
 *
 * @return 0 on success, <0 else
 */
int nicif_appctx_add(uint16_t appid, uint32_t db, uint64_t *rxq_base,
    uint32_t rxq_len, uint64_t *txq_base, uint32_t txq_len, int evfd);

/** Flags for connections (used in nicif_connection_add()) */
enum nicif_connection_flags {
  /** Enable object steering for connection. */
  NICIF_CONN_OBJCONN    = (1 <<  0),
  /** No hashing for object key for steering. */
  NICIF_CONN_OBJNOHASH  = (1 <<  1),
  /** Enable ECN for connection. */
  NICIF_CONN_ECN        = (1 <<  2),
};

/**
 * Register flow (must be called from poll thread).
 *
 * @param db          Doorbell ID
 * @param mac_remote  MAC address of the remote host
 * @param ip_local    Local IP address
 * @param port_local  Local port number
 * @param ip_remote   Remote IP address
 * @param port_remote Remote port number
 * @param rx_base     Base address of circular receive buffer
 * @param rx_len      Length of circular receive buffer
 * @param tx_base     Base address of circular transmit buffer
 * @param tx_len      Length of circular transmit buffer
 * @param remote_seq  Next sequence number expected from remote host
 * @param local_seq   Next sequence number for transmission
 * @param app_opaque  Opaque value to pass in notificaitions
 * @param flags       See #nicif_connection_flags.
 * @param rate        Congestion rate to set [Kbps]
 * @param fn_core     FlexNIC emulator core for the connection
 * @param flow_group  Flow group
 * @param pf_id       Pointer to location where flow id should be stored
 *
 * @return 0 on success, <0 else
 */
int nicif_connection_add(uint32_t db, uint64_t mac_remote, uint32_t ip_local,
    uint16_t port_local, uint32_t ip_remote, uint16_t port_remote,
    uint64_t rx_base, uint32_t rx_len, uint64_t tx_base, uint32_t tx_len,
    uint32_t remote_seq, uint32_t local_seq, uint64_t app_opaque,
    uint32_t flags, uint32_t rate, uint32_t fn_core, uint16_t flow_group,
    uint32_t *pf_id);

/**
 * Move flow to new db.
 *
 * @param dst_db  New doorbell ID
 * @param f_id    ID of flow to be moved
 *
 * @return 0 on success, <0 else
 */
int nicif_connection_move(uint32_t dst_db, uint32_t f_id);

/**
 * Connection statistics for congestion control
 * (see nicif_connection_stats()).
 */
struct nicif_connection_stats {
  /** Number of dropped segments */
  uint32_t c_drops;
  /** Number of ACKs received */
  uint32_t c_acks;
  /** Acknowledged bytes */
  uint32_t c_ackb;
  /** Number of ACKs with ECN marks */
  uint32_t c_ecn;
  /** Has pending data in transmit buffer */
  int txp;
  /** Current rtt estimate */
  uint32_t rtt;
};

/**
 * Read connection stats from NIC.
 *
 * @param f_id    ID of flow
 * @param p_stats Pointer to statistics structs.
 *
 * @return 0 on success, <0 else
 */
int nicif_connection_stats(uint32_t f_id,
    struct nicif_connection_stats *p_stats);

/**
 * Set rate for flow.
 *
 * @param f_id  ID of flow
 * @param rate  Rate to set [Kbps]
 *
 * @return 0 on success, <0 else
 */
int nicif_connection_setrate(uint32_t f_id, uint32_t rate);

/**
 * Mark flow for retransmit after timeout.
 *
 * @param f_id ID of flow
 * @param flow_group FlexNIC flow group
 *
 * @return 0 on success, <0 else
 */
int nicif_connection_retransmit(uint32_t f_id, uint16_t core);

/**
 * Allocate transmit buffer for raw packet.
 *
 * TODO: we probably want an asynchronous version of this.
 *
 * @param len     Length of packet to be sent
 * @param buf     Pointer to location where base address will be stored
 * @param opaque  Pointer to location to store opaque value that needs to be
 *                passed to nicif_tx_send().
 *
 * @return 0 on success, <0 else
 */
int nicif_tx_alloc(uint16_t len, void **buf, uint32_t *opaque);

/**
 * Actually send out transmit buffer (lens need to match).
 *
 * @param opaque Opaque value returned from nicif_tx_alloc().
 *
 * @return 0 on success, <0 else
 */
void nicif_tx_send(uint32_t opaque);

/** @} */

/*****************************************************************************/
/**
 * @addtogroup kernel-packetmem
 * @brief Packet Memory Manager.
 * @ingroup kernel
 *
 * Manages memory region that can be used by FlexNIC for DMA.
 * @{ */

struct packetmem_handle;

/** Initialize packet memory interface */
int packetmem_init(void);

/**
 * Allocate packet memory of specified length.
 *
 * @param length  Required number of bytes
 * @param off     Pointer to location where offset in DMA region should be
 *                stored
 * @param handle  Pointer to location where handle for memory region should be
 *                stored
 *
 * @return 0 on success, <0 else
 */
int packetmem_alloc(size_t length, uintptr_t *off,
    struct packetmem_handle **handle);

/**
 * Free packet memory region.
 *
 * @param handle  Handle for memory region to be freed
 *
 * @return 0 on success, <0 else
 */
void packetmem_free(struct packetmem_handle *handle);

/** @} */

/*****************************************************************************/
/**
 * @addtogroup kernel-appif
 * @brief Application Interface.
 * @ingroup kernel
 *
 * This is implemented in appif.c and appif_ctx.c
 * @{ */

/** Initialize application interface */
int appif_init(void);

/** Poll application in memory queues */
unsigned appif_poll(void);

/**
 * Callback from tcp_open(): Connection open done.
 *
 * @param c       Connection
 * @param status  Status: 0 if successful
 */
void appif_conn_opened(struct connection *c, int status);

/**
 * Callback from TCP module: New connection request received on listener.
 *
 * @param l           Listener that received new connection
 * @param remote_ip   Remote IP address
 * @param remote_port Remote port
 */
void appif_listen_newconn(struct listener *l, uint32_t remote_ip,
    uint16_t remote_port);

/**
 * Callback from tcp_accept(): Connection accepted.
 *
 * @param c       Connection passed to tcp_accept
 * @param status  Status: 0 if successful
 */
void appif_accept_conn(struct connection *c, int status);

/** @} */

/*****************************************************************************/
/**
 * @addtogroup kernel-tcp
 * @brief TCP Protocol Handling
 * @ingroup kernel
 * @{ */

/** TCP connection state machine state. */
enum connection_status {
  /** Accepted: waiting for a SYN. */
  CONN_SYN_WAIT,
  /** Opening: waiting for ARP request. */
  CONN_ARP_PENDING,
  /** Opening: SYN request sent. */
  CONN_SYN_SENT,
  /** Opening: SYN received, waiting for NIC registration. */
  CONN_REG_SYNACK,
  /** Connection opened. */
  CONN_OPEN,
  /** Connection failed. */
  CONN_FAILED,
};

/** Congestion control data for window-based DCTCP */
struct connection_cc_dctcp_win {
  /** Rate of ECN bits received. */
  uint32_t ecn_rate;
  /** Congestion window. */
  uint32_t window;
  /** Flag indicating whether flow is in slow start. */
  int slowstart;
};

/** Congestion control data for window-based DCTCP */
struct connection_cc_dctcp_rate {
  /** Unprocessed acks */
  uint32_t unproc_acks;
  /** Unprocessed ack bytes */
  uint32_t unproc_ackb;
  /** Unprocessed ECN acks */
  uint32_t unproc_ecn;
  /** Unprocessed drops */
  uint32_t unproc_drops;

  /** Rate of ECN bits received. */
  uint32_t ecn_rate;
  /** Actual rate. */
  uint32_t act_rate;
  /** Flag indicating whether flow is in slow start. */
  int slowstart;
};

/** Congestion control data for TIMELY */
struct connection_cc_timely {
  /** Previous RTT. */
  uint32_t rtt_prev;
  /** RTT gradient. */
  int32_t rtt_diff;
  /** HAI counter. */
  uint32_t hai_cnt;
  /** Actual rate. */
  uint32_t act_rate;
  /** Last timestamp. */
  uint32_t last_ts;
  /** Flag indicating whether flow is in slow start. */
  int slowstart;
};

/** TCP connection state */
struct connection {
  /**
   * @name Application interface
   * @{
   */
    /** Application-specified opaque value for connection. */
    uint64_t opaque;
    /** Application context this connection is assigned to. */
    struct app_context *ctx;
    /** New application context if connection should be moved. */
    struct app_context *new_ctx;
    /** Link list pointer for application connections. */
    struct connection *app_next;
    /** Doorbell id. */
    uint32_t db_id;
  /**@}*/

  /**
   * @name Data buffers
   * @{
   */
    /** Memory manager handle for receive buffer. */
    struct packetmem_handle *rx_handle;
    /** Memory manager handle for transmit buffer. */
    struct packetmem_handle *tx_handle;
    /** Receive buffer pointer. */
    uint8_t *rx_buf;
    /** Transmit buffer pointer. */
    uint8_t *tx_buf;
    /** Receive buffer size. */
    uint32_t rx_len;
    /** Transmit buffer size. */
    uint32_t tx_len;
  /**@}*/

  /**
   * @name Address information
   * @{
   */
    /** Peer MAC address for connection. */
    uint64_t remote_mac;
    /** Peer IP address. */
    uint32_t remote_ip;
    /** Local IP to be used. */
    uint32_t local_ip;
    /** Peer port number. */
    uint16_t remote_port;
    /** Local port number. */
    uint16_t local_port;
  /**@}*/

  /**
   * @name Connection state
   * @{
   */
    /** Current connection state machine state. */
    enum connection_status status;
    /** Peer sequence number. */
    uint32_t remote_seq;
    /** Local sequence number. */
    uint32_t local_seq;
    /** Timestamp received with SYN/SYN-ACK packet */
    uint32_t syn_ts;
  /**@}*/

  /**
   * @name Timeouts
   * @{
   */
    /** Timeout in microseconds (used for handshake). */
    uint32_t timeout;
    /** Timeout object. */
    struct timeout to;
    /** Number of times timout triggered. */
    int to_attempts;
    /** 1 if timeout is currently armed. */
    int to_armed;
  /**@}*/

  /**
   * @name Congestion control
   * @{
   */
    /** Timestamp when control loop ran last */
    uint32_t cc_last_ts;
    /** Last rtt estimate */
    uint32_t cc_rtt;
    /** Congestion rate limit. */
    uint32_t cc_rate;
    /** Had retransmits. */
    uint32_t cc_rexmits;
    /** Data for CC algorithm. */
    union {
      /** Window-based dctcp */
      struct connection_cc_dctcp_win dctcp_win;
      /** TIMELY */
      struct connection_cc_timely timely;
      /** Rate-based dctcp */
      struct connection_cc_dctcp_rate dctcp_rate;
    } cc;
    /** #control intervals with data in tx buffer but no ACKs */
    uint32_t cnt_tx_pending;
    /** Timestamp when flow was first not moving */
    uint32_t ts_tx_pending;
  /**@}*/

  /** Linked list for global connection list. */
  struct connection *hash_next;
  /** Asynchronous completion information. */
  struct nicif_completion comp;
  /** NIC flow state ID. */
  uint32_t flow_id;
  /** FlexNIC emulator core. */
  uint32_t fn_core;
  /** Flags: see #nicif_connection_flags */
  uint32_t flags;
  /** Flow group (RSS bucket for steering). */
  uint16_t flow_group;
};

/** TCP listener  */
struct listener {
  /**
   * @name Application interface
   * @{
   */
    /** Application-specified opaque value for listener. */
    uint64_t opaque;
    /** Application context this listener is assigned to. */
    struct app_context *ctx;
    /** Link list pointer for application listeners. */
    struct listener *app_next;
    /** Doorbell id. */
    uint32_t db_id;
  /**@}*/

  /**
   * @name Backlog queue
   * @{
   */
    /** Backlog queue total length. */
    uint32_t backlog_len;
    /** Next entry in backlog queue. */
    uint32_t backlog_pos;
    /** Number of entries used in backlog queue. */
    uint32_t backlog_used;
    /** Backlog queue buffers */
    void **backlog_ptrs;
    /** Backlog core id array */
    uint32_t *backlog_cores;
    /** Backlog flow group array */
    uint16_t *backlog_fgs;
  /**@}*/

  /** List of waiting connections from accept calls */
  struct connection *wait_conns;
  /** Listener port */
  uint16_t port;
  /** Flags: see #nicif_connection_flags */
  uint32_t flags;
};

/** List of tcp connections */
extern struct connection *tcp_conns;

/** Initialize TCP subsystem */
int tcp_init(void);

/** Poll for TCP events */
void tcp_poll(void);

/**
 * Open a connection.
 *
 * This function returns asynchronously if it does not fail immediately. The TCP
 * module will call appif_conn_opened().
 *
 * @param ctx         Application context
 * @param opaque      Opaque value passed from application
 * @param remote_ip   Remote IP address
 * @param remote_port Remote port number
 * @param db_id       Doorbell ID to use for connection
 * @param objconn     != 0 if opening an object connection
 * @param objnohash   != 0 to disable hashing on object connection
 * @param conn        Pointer to location for storing pointer of created conn
 *                    struct.
 *
 * @return 0 on success, <0 else
 */
int tcp_open(struct app_context *ctx, uint64_t opaque, uint32_t remote_ip,
    uint16_t remote_port, uint32_t db_id, int objconn, int objnohash,
    struct connection **conn);

/**
 * Open a listener.
 *
 * @param ctx         Application context
 * @param opaque      Opaque value passed from application
 * @param local_port  Port to listen on
 * @param backlog     Backlog queue length
 * @param reuseport   Enable reuseport, to have multiple listeners for the same
 *                    port.
 * @param objconn     != 0 to create a listener for object connections
 * @param objnohash   != 0 to disable hashing for object connections
 * @param listen      Pointer to location for storing pointer of created
 *                    listener struct.
 *
 * @return 0 on success, <0 else
 */
int tcp_listen(struct app_context *ctx, uint64_t opaque, uint16_t local_port,
    uint32_t backlog, int reuseport, int objconn, int objnohash,
    struct listener **listen);

/**
 * Prepare to receive a connection on a listener.
 *
 * @param ctx     Application context
 * @param opaque  Opaque value passed from application
 * @param listen  Listener
 * @param db_id   Doorbell ID
 *
 * @return 0 on success, <0 else
 */
int tcp_accept(struct app_context *ctx, uint64_t opaque,
        struct listener *listen, uint32_t db_id);

/**
 * RX processing for a TCP packet.
 *
 * @param pkt Pointer to packet
 * @param len Length of packet
 * @param fn_core FlexNIC emulator core
 * @param flow_group Flow group (rss bucket for steering)
 */
void tcp_packet(const void *pkt, uint16_t len, uint32_t fn_core,
    uint16_t flow_group);

/**
 * Destroy already closed/failed connection.
 *
 * @param conn  Connection
 */
void tcp_destroy(struct connection *conn);

/**
 * TCP timeout triggered.
 *
 * @param to    Timeout that triggered
 * @param type  Timeout type
 */
void tcp_timeout(struct timeout *to, enum timeout_type type);

/** @} */

/*****************************************************************************/
/**
 * @addtogroup kernel-cc
 * @brief Congestion Control
 * @ingroup kernel
 * @{ */

/** Initialize congestion control management */
int cc_init(void);

/**
 * Poll congestion control
 *
 * @param cur_ts Current timestamp in micro seconds.
 */
unsigned cc_poll(uint32_t cur_ts);

uint32_t cc_next_ts(uint32_t cur_ts);

/**
 * Initialize congestion state for flow
 *
 * @param conn Connection to initialize.
 */
void cc_conn_init(struct connection *conn);

/** @} */

/*****************************************************************************/
/**
 * @addtogroup kernel-arp
 * @brief ARP Protocol Handling
 * @ingroup kernel
 * @{ */

/** Initialize ARP subsystem */
int arp_init(void);

/**
 * Resolve IP address to MAC address using ARP resolution.
 *
 * This function can either return success immediately in case on an ARP cache
 * hit, or return asynchronously if an ARP request was sent out.
 *
 * @param comp  Context for asynchronous return
 * @param ip    IP address to be resolved
 * @param mac   Pointer of memory location where destination MAC should be
 *              stored.
 *
 * @return 0 on success, < 0 on error, and > 0 if request was sent but response
 *    is still pending.
 */
int arp_request(struct nicif_completion *comp, uint32_t ip, uint64_t *mac);

/**
 * RX processing for an ARP packet.
 *
 * @param pkt Pointer to packet
 * @param len Length of packet
 */
void arp_packet(const void *pkt, uint16_t len);

/**
 * ARP timeout triggered.
 *
 * @param to    Timeout that triggered
 * @param type  Timeout type
 */
void arp_timeout(struct timeout *to, enum timeout_type type);

/** @} */

/*****************************************************************************/
/**
 * @addtogroup kernel-routing
 * @brief IP routing
 * @ingroup kernel
 * @{ */

/** Initialize IP routing subsystem */
int routing_init(void);

/**
 * Resolve IP address to MAC address using routing and ARP.
 *
 * This function can either return success immediately, or asynchronously.
 *
 * @param comp  Context for asynchronous return
 * @param ip    IP address to be resolved
 * @param mac   Pointer of memory location where destination MAC should be
 *              stored.
 *
 * @return 0 on success, < 0 on error, and > 0 for asynchronous return.
 */
int routing_resolve(struct nicif_completion *comp, uint32_t ip, uint64_t *mac);

static inline void send_network_raw(uint8_t* buf, uint16_t len)
{
        uint32_t new_tail;
        void* p;

        /** allocate send buffer */
        if (nicif_tx_alloc(len, (void **) &p, &new_tail) != 0) {
                fprintf(stderr, "send_control failed\n");
                exit(-1);
        }
        nicif_tx_send(new_tail);
}

/** @} */

#endif // ndef INTERNAL_H_
