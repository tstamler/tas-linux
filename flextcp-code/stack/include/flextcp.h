#ifndef FLEXTCP_H_
#define FLEXTCP_H_

/**
 * @file flextcp.h
 * @brief Public low-level interface for application flextcp stack.
 *
 * @addtogroup app-stack
 * @brief Application library flextcp stack (low-level interface)
 */

#include <stdint.h>

#define FLEXTCP_MAX_CONTEXTS 32
#define FLEXTCP_MAX_FTCPCORES 16

/**
 * A flextcp context is per-thread state for the stack. (opaque)
 * This includes:
 *   - admin queue pair to kernel
 *   - notification queue pair to flexnic
 */
struct flextcp_context {
  /* incoming queue from the kernel */
  void *kin_base;
  uint32_t kin_len;
  uint32_t kin_head;

  /* outgoing queue to the kernel */
  void *kout_base;
  uint32_t kout_len;
  uint32_t kout_head;

  /* queues from NIC cores */
  uint32_t rxq_len;
  uint32_t txq_len;
  struct {
    void *txq_base;
    void *rxq_base;
    uint32_t rxq_head;
    uint32_t txq_tail;
  } queues[FLEXTCP_MAX_FTCPCORES];

  /* list of connections with pending updates for NIC */
  struct flextcp_connection *bump_pending_first;
  struct flextcp_connection *bump_pending_last;

  /* other */
  uint16_t db_id;
  uint16_t ctx_id;

  uint16_t num_queues;
  uint16_t next_queue;

  int epfd, evfd;
};

/** TCP listening "socket". (opaque) */
struct flextcp_listener {
  struct flextcp_connection *conns;

  uint16_t local_port;
  uint8_t status;
};

/** TCP connection. (opaque) */
struct flextcp_connection {
  /* rx buffer */
  uint8_t *rxb_base;
  uint32_t rxb_len;
  uint32_t rxb_head;
  uint32_t rxb_tail;
  uint32_t rxb_nictail;

  /* tx buffer */
  uint8_t *txb_base;
  uint32_t txb_len;
  uint32_t txb_head;
  uint32_t txb_head_alloc;
  uint32_t txb_tail;
  uint32_t txb_nichead;

  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;

  uint32_t seq_rx;
  uint32_t seq_tx;

  uint32_t flow_id;
  uint32_t bump_seq;

  struct flextcp_connection *bump_next;
  struct flextcp_connection *bump_prev;
  uint16_t fn_core;
  uint8_t bump_pending;
  uint8_t status;
};

/** Object TCP listening "socket". (opaque) */
struct flextcp_obj_listener {
  struct flextcp_listener l;
};

/** Context-local information for connection (not part of public interface) */
struct flextcp_obj_conn_ctx {
  uint32_t obj_pos;
  uint32_t obj_len_rem;
};

/** Object TCP connection. (opaque) */
struct flextcp_obj_connection {
  struct flextcp_obj_conn_ctx ctx[FLEXTCP_MAX_CONTEXTS];
  struct flextcp_connection c;
  volatile uint8_t lock;
};

/** Object TCP handle for allocated object (opaque) */
struct flextcp_obj_handle {
  uint32_t pos;
};

/** Types of events that can occur in flextcp contexts */
enum flextcp_event_type {
  /** flextcp_listen_open() result. */
  FLEXTCP_EV_LISTEN_OPEN,
  /** New connection on listening socket arrived. */
  FLEXTCP_EV_LISTEN_NEWCONN,
  /** Accept operation completed */
  FLEXTCP_EV_LISTEN_ACCEPT,

  /** flextcp_connection_open() result */
  FLEXTCP_EV_CONN_OPEN,
  /** Connection was closed */
  FLEXTCP_EV_CONN_CLOSED,
  /** Data arrived on connection */
  FLEXTCP_EV_CONN_RECEIVED,
  /** More send buffer available */
  FLEXTCP_EV_CONN_SENDBUF,
  /** Connection moved to new context */
  FLEXTCP_EV_CONN_MOVED,


  /** flextcp_obj_listen_open() result */
  FLEXTCP_EV_OBJ_LISTEN_OPEN,
  /** New connection on listening socket arrived. */
  FLEXTCP_EV_OBJ_LISTEN_NEWCONN,
  /** Accept operation completed */
  FLEXTCP_EV_OBJ_LISTEN_ACCEPT,

  /** flextcp_obj_connection_open() result */
  FLEXTCP_EV_OBJ_CONN_OPEN,
  /** Connection was closed */
  FLEXTCP_EV_OBJ_CONN_CLOSED,
  /** Data arrived on connection */
  FLEXTCP_EV_OBJ_CONN_RECEIVED,
  /** More send buffer available */
  FLEXTCP_EV_OBJ_CONN_SENDBUF,
};

/** Events that can occur on flextcp contexts. */
struct flextcp_event {
  uint8_t event_type;
  union {
    /** For #FLEXTCP_EV_LISTEN_OPEN */
    struct {
      int16_t status;
      struct flextcp_listener *listener;
    } listen_open;
    /** For #FLEXTCP_EV_LISTEN_NEWCONN */
    struct {
      uint16_t remote_port;
      uint32_t remote_ip;
      struct flextcp_listener *listener;
    } listen_newconn;
    /** For #FLEXTCP_EV_LISTEN_ACCEPT */
    struct {
      int16_t status;
      struct flextcp_connection *conn;
    } listen_accept;

    /** For #FLEXTCP_EV_CONN_OPEN */
    struct {
      int16_t status;
      struct flextcp_connection *conn;
    } conn_open;
    /** For #FLEXTCP_EV_CONN_RECEIVED */
    struct {
      void *buf;
      size_t len;
      struct flextcp_connection *conn;
    } conn_received;
    /** For #FLEXTCP_EV_CONN_SENDBUF */
    struct {
      struct flextcp_connection *conn;
    } conn_sendbuf;
    /** For #FLEXTCP_EV_CONN_MOVED */
    struct {
      int16_t status;
      struct flextcp_connection *conn;
    } conn_moved;


    struct {
      int16_t status;
      struct flextcp_obj_listener *listener;
    } obj_listen_open;
    struct {
      uint16_t remote_port;
      uint32_t remote_ip;
      struct flextcp_obj_listener *listener;
    } obj_listen_newconn;
    struct {
      int16_t status;
      struct flextcp_obj_connection *conn;
    } obj_listen_accept;

    struct {
      int16_t status;
      struct flextcp_obj_connection *conn;
    } obj_conn_open;
    struct {
      uint8_t dstlen;
      uint32_t len_1;
      uint32_t len_2;
      struct flextcp_obj_handle handle;

      void *buf_1;
      void *buf_2;

      struct flextcp_obj_connection *conn;
    } obj_conn_received;
    struct {
      struct flextcp_obj_connection *conn;
    } obj_conn_sendbuf;
  } ev;
};

#define FLEXTCP_LISTEN_REUSEPORT 0x1
#define FLEXTCP_LISTEN_OBJNOHASH 0x2

#define FLEXTCP_CONNECT_OBJNOHASH 0x1

/**
 * Initializes global flextcp state, must only be called once.
 * @return 0 on success, < 0 on failure
 */
int flextcp_init(void);

/**
 * Create a flextcp context.
 */
int flextcp_context_create(struct flextcp_context *ctx);

/**
 * Poll events from a flextcp socket.
 */
int flextcp_context_poll(struct flextcp_context *ctx, int num,
    struct flextcp_event *events);



/*****************************************************************************/
/* Regular TCP connection management */

/** Open a listening socket (asynchronous). */
int flextcp_listen_open(struct flextcp_context *ctx,
    struct flextcp_listener *lst, uint16_t port, uint32_t backlog,
    uint32_t flags);

/** Accept connections on a listening socket (asynchronous). This can be called
 * more than once to register multiple connection handles. */
int flextcp_listen_accept(struct flextcp_context *ctx,
    struct flextcp_listener *lst, struct flextcp_connection *conn);


/** Open a connection (asynchronous). */
int flextcp_connection_open(struct flextcp_context *ctx,
    struct flextcp_connection *conn, uint32_t dst_ip, uint16_t dst_port);

/** Receive processing for `len' bytes done. */
int flextcp_connection_rx_done(struct flextcp_context *ctx, struct flextcp_connection *conn, size_t len);

/** Allocate transmit buffer for `len' bytes, returns number of bytes
 * allocated.
 *
 * NOTE: short allocs can occur if buffer wraps around
 */
ssize_t flextcp_connection_tx_alloc(struct flextcp_connection *conn, size_t len,
    void **buf);

/** Allocate transmit buffer for `len' bytes, returns number of bytes
 * allocated. May be split across two buffers, in case of wrap around. */
ssize_t flextcp_connection_tx_alloc2(struct flextcp_connection *conn, size_t len,
    void **buf_1, size_t *len_1, void **buf_2);

/** Send previously allocated bytes in transmit buffer */
int flextcp_connection_tx_send(struct flextcp_context *ctx,
        struct flextcp_connection *conn, size_t len);

/** Make sure there is room in the context send queue (not send buffer)
 *
 * Returns 0 if transmit is possible, -1 otherwise.
 */
int flextcp_connection_tx_possible(struct flextcp_context *ctx,
    struct flextcp_connection *conn);

/** Move connection to specfied context */
int flextcp_connection_move(struct flextcp_context *ctx,
        struct flextcp_connection *conn);



/*****************************************************************************/
/* Object TCP connections */

/** Open a object listening socket (asynchronous). */
int flextcp_obj_listen_open(struct flextcp_context *ctx,
    struct flextcp_obj_listener *lst, uint16_t port, uint32_t backlog,
    uint32_t flags);

/** Accept connections on a listening object socket (asynchronous). This can be
 * called more than once to register multiple connection handles. */
int flextcp_obj_listen_accept(struct flextcp_context *ctx,
    struct flextcp_obj_listener *lst, struct flextcp_obj_connection *conn);

/** Open an object connection (asynchronous). */
int flextcp_obj_connection_open(struct flextcp_context *ctx,
    struct flextcp_obj_connection *conn, uint32_t dst_ip, uint16_t dst_port,
    uint32_t flags);


/** Received object fully processed. */
void flextcp_obj_connection_rx_done(struct flextcp_context *ctx,
    struct flextcp_obj_connection *conn, struct flextcp_obj_handle *oh);

/** Allocate object for transmission (note that in contrast to
 * flextcp_connection_tx_alloc there won't be short allocs). */
int flextcp_obj_connection_tx_alloc(struct flextcp_obj_connection *oconn,
    uint8_t dstlen, size_t len, void **buf1, size_t *len1, void **buf2,
    struct flextcp_obj_handle *oh);

/** Send out allocated object */
void flextcp_obj_connection_tx_send(struct flextcp_context *ctx,
        struct flextcp_obj_connection *conn, struct flextcp_obj_handle *oh);

/** Bump NIC pointers for rx and tx if necessary */
int flextcp_obj_connection_bump(struct flextcp_context *ctx,
        struct flextcp_obj_connection *conn);

void flextcp_block(struct flextcp_context *ctx, int timeout_ms);

#endif /* ndef FLEXTCP_H_ */
