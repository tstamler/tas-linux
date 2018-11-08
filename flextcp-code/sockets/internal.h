#ifndef INTERNAL_H_
#define INTERNAL_H_

#include <stdint.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>

#include <flextcp.h>

enum filehandle_type {
  SOCK_UNUSED = 0,
  SOCK_SOCKET = 1,
  SOCK_CONNECTION = 2,
  SOCK_LISTENER = 3,
};

enum socket_flags {
  SOF_NONBLOCK = 1,
  SOF_BOUND = 2,
  SOF_REUSEPORT = 4,
};

enum conn_status {
  SOC_CONNECTING = 0,
  SOC_CONNECTED = 1,
  SOC_FAILED = 2,
};

enum listen_status {
  SOL_OPENING = 0,
  SOL_OPEN = 1,
  SOL_FAILED = 2,
};

struct socket_pending {
  struct socket *s;
  struct flextcp_context *ctx;
  struct socket_pending *next;
  int fd;
};

struct socket {
  union {
    struct {
      struct flextcp_connection c;
      uint8_t status;
      struct socket *listener;

      void *rx_buf_1;
      void *rx_buf_2;
      size_t rx_len_1;
      size_t rx_len_2;
      struct flextcp_context *ctx;
      int move_status;
    } connection;
    struct {
      struct flextcp_listener l;
      struct socket_pending *pending;
      int backlog;
      uint8_t status;
    } listener;
  } data;
  struct sockaddr_in addr;
  uint8_t flags;
  uint8_t type;

  /** epoll events currently active on this socket */
  uint32_t ep_events;
  /** epoll fds without EPOLLEXCLUSIVE */
  struct epoll_socket *eps;
#if 0
  /** first epoll fd with EPOLLEXCLUSIVE */
  struct epoll_socket *eps_exc_first;
  /** last epoll fd with EPOLLEXCLUSIVE */
  struct epoll_socket *eps_exc_last;
#endif
};

struct epoll {
  /** list of sockets that don't have any unmasked events pending */
  struct epoll_socket *inactive;
  /** list of sockets with unmasked events pending */
  struct epoll_socket *active_first;
  struct epoll_socket *active_last;

  uint32_t num_linux;
  uint32_t num_active;
  uint8_t linux_cnt;
};

struct epoll_socket {
  struct epoll *ep;
  struct socket *s;

  struct epoll_socket *ep_next;
  struct epoll_socket *ep_prev;

  struct epoll_socket *so_prev;
  struct epoll_socket *so_next;

  epoll_data_t data;
  uint32_t mask;
  uint8_t active;
};

int flextcp_fd_init(void);
int flextcp_fd_salloc(struct socket **ps);
int flextcp_fd_ealloc(struct epoll **pe, int fd);
int flextcp_fd_slookup(int fd, struct socket **ps);
int flextcp_fd_elookup(int fd, struct epoll **pe);
void flextcp_fd_release(int fd);
void flextcp_fd_close(int fd);

struct flextcp_context *flextcp_sockctx_get(void);
int flextcp_sockctx_poll(struct flextcp_context *ctx);
int flextcp_sockctx_poll_n(struct flextcp_context *ctx, unsigned n);

void flextcp_epoll_sockinit(struct socket *s);
void flextcp_epoll_set(struct socket *s, uint32_t evts);
void flextcp_epoll_clear(struct socket *s, uint32_t evts);

#endif /* ndef INTERNAL_H_ */
