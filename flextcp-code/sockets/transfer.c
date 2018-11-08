#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>

#include <utils.h>
#include <utils_circ.h>
#include <flextcp_sockets.h>
#include <flextcp.h>

#include "internal.h"
#include "../stack/internal.h"

ssize_t _SF(recvmsg)(int sockfd, struct msghdr *msg, int flags)
{
  struct socket *s;
  struct flextcp_context *ctx;
  ssize_t ret = 0;
  size_t len, i, off;
  struct iovec *iov;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  /* not a connection, or not connected */
  if (s->type != SOCK_CONNECTION ||
      s->data.connection.status != SOC_CONNECTED)
  {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  /* return 0 if 0 length */
  len = 0;
  iov = msg->msg_iov;
  for (i = 0; i < msg->msg_iovlen; i++) {
    len += iov[i].iov_len;
  }
  if (len == 0) {
    goto out;
  }

  ctx = flextcp_sockctx_get();

  /* wait for data if necessary, or abort if non-blocking */
  if (s->data.connection.rx_len_1 == 0) {
    flextcp_epoll_clear(s, EPOLLIN);
    if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK) {
      errno = EAGAIN;
      ret = -1;
      goto out;
    } else {
      while (s->data.connection.rx_len_1 == 0) {
        flextcp_sockctx_poll(ctx);
      }
    }
  }

  /* static size_t all_received = 0; */

  /*  */
  for (i = 0; i < msg->msg_iovlen && s->data.connection.rx_len_1 > 0; i++) {
    off = 0;
    if (s->data.connection.rx_len_1 <= iov[i].iov_len) {
      off = s->data.connection.rx_len_1;
      memcpy(iov[i].iov_base, s->data.connection.rx_buf_1, off);
      ret += off;

      s->data.connection.rx_buf_1 = s->data.connection.rx_buf_2;
      s->data.connection.rx_len_1 = s->data.connection.rx_len_2;
      s->data.connection.rx_buf_2 = NULL;
      s->data.connection.rx_len_2 = 0;
    }

    len = MIN(iov[i].iov_len - off, s->data.connection.rx_len_1);
    memcpy((uint8_t *) iov[i].iov_base + off, s->data.connection.rx_buf_1, len);
    ret += len;

    s->data.connection.rx_buf_1 = (uint8_t *) s->data.connection.rx_buf_1 + len;
    s->data.connection.rx_len_1 -= len;
  }

  if (ret > 0) {
    if (s->data.connection.rx_len_1 == 0) {
      flextcp_epoll_clear(s, EPOLLIN);
    }
    flextcp_connection_rx_done(ctx, &s->data.connection.c, ret);
  }
out:
  flextcp_fd_release(sockfd);
  return ret;
}

static inline ssize_t recv_simple(int sockfd, void *buf, size_t len, int flags)
{
  struct socket *s;
  struct flextcp_context *ctx;
  ssize_t ret = 0;
  size_t off, len_2;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  /* not a connection, or not connected */
  if (s->type != SOCK_CONNECTION ||
      s->data.connection.status != SOC_CONNECTED)
  {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  /* return 0 if 0 length */
  if (len == 0) {
    goto out;
  }

  ctx = flextcp_sockctx_get();

  /* wait for data if necessary, or abort if non-blocking */
  if (s->data.connection.rx_len_1 == 0) {
    flextcp_epoll_clear(s, EPOLLIN);
    if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK) {
      errno = EAGAIN;
      ret = -1;
      goto out;
    } else {
      while (s->data.connection.rx_len_1 == 0) {
        flextcp_sockctx_poll(ctx);
      }
    }
  }

  /* copy to provided buffer */
  off = 0;
  if (s->data.connection.rx_len_1 <= len) {
    memcpy(buf, s->data.connection.rx_buf_1, s->data.connection.rx_len_1);
    ret = off = s->data.connection.rx_len_1;

    s->data.connection.rx_buf_1 = s->data.connection.rx_buf_2;
    s->data.connection.rx_len_1 = s->data.connection.rx_len_2;
    s->data.connection.rx_buf_2 = NULL;
    s->data.connection.rx_len_2 = 0;
  }
  len_2 = MIN(s->data.connection.rx_len_1, len - off);
  memcpy((uint8_t *) buf + ret, s->data.connection.rx_buf_1, len_2);
  ret += len_2;
  s->data.connection.rx_buf_1 += len_2;
  s->data.connection.rx_len_1 -= len_2;

  if (ret > 0) {
    if (s->data.connection.rx_len_1 == 0) {
      flextcp_epoll_clear(s, EPOLLIN);
    }
    flextcp_connection_rx_done(ctx, &s->data.connection.c, ret);
  }
out:
  flextcp_fd_release(sockfd);
  return ret;
}

#include <unistd.h>

ssize_t _SF(sendmsg)(int sockfd, const struct msghdr *msg, int flags)
{

  struct socket *s;
  struct flextcp_context *ctx;
  ssize_t ret = 0;
  size_t len, i, l, len_1, len_2, off;
  struct iovec *iov;
  void *dst_1, *dst_2;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  /* not a connection, or not connected */
  if (s->type != SOCK_CONNECTION ||
      s->data.connection.status != SOC_CONNECTED)
  {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  /* return 0 if 0 length */
  len = 0;
  iov = msg->msg_iov;
  for (i = 0; i < msg->msg_iovlen; i++) {
    len += iov[i].iov_len;
  }
  if (len == 0) {
    goto out;
  }

  ctx = flextcp_sockctx_get();

  /* make sure there is space in the transmit queue if the socket is
   * non-blocking */
  if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK &&
      flextcp_connection_tx_possible(ctx, &s->data.connection.c) != 0)
  {
    errno = EAGAIN;
    ret = -1;
    goto out;
  }

  /* allocate transmit buffer */
  ret = flextcp_connection_tx_alloc2(&s->data.connection.c, len, &dst_1, &len_1,
      &dst_2);
  if (ret < 0) {
    fprintf(stderr, "sendmsg: flextcp_connection_tx_alloc failed\n");
    abort();
  }

  if (ret == 0) {
    if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK) {
      errno = EAGAIN;
      ret = -1;
      goto out;
    } else {
      do {
        flextcp_sockctx_poll(ctx);

        ret = flextcp_connection_tx_alloc2(&s->data.connection.c, len, &dst_1,
            &len_1, &dst_2);
        if (ret < 0) {
          fprintf(stderr, "sendmsg: flextcp_connection_tx_alloc failed\n");
          abort();
        }
      } while (ret == 0);
    }
  }
  len_2 = ret - len_1;

  /* copy into TX buffer */
  len = ret;
  iov = msg->msg_iov;
  off = 0;
  for (i = 0; i < msg->msg_iovlen && len > 0; i++) {
    l = MIN(len, iov[i].iov_len);
    split_write(iov[i].iov_base, l, dst_1, len_1, dst_2, len_2, off);

    len -= l;
    off += l;
  }

  /* send out */
  /* TODO: this should not block for non-blocking sockets */
  while (flextcp_connection_tx_send(ctx, &s->data.connection.c, ret) != 0) {
    flextcp_sockctx_poll(ctx);
  }

out:
  flextcp_fd_release(sockfd);
  return ret;
}

static inline ssize_t send_simple(int sockfd, const void *buf, size_t len,
    int flags)
{
  struct socket *s;
  struct flextcp_context *ctx;
  ssize_t ret = 0;
  size_t len_1, len_2;
  void *dst_1, *dst_2;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  /* not a connection, or not connected */
  if (s->type != SOCK_CONNECTION ||
      s->data.connection.status != SOC_CONNECTED)
  {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  /* return 0 if 0 length */
  if (len == 0) {
    goto out;
  }

  ctx = flextcp_sockctx_get();

  /* make sure there is space in the transmit queue if the socket is
   * non-blocking */
  if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK &&
      flextcp_connection_tx_possible(ctx, &s->data.connection.c) != 0)
  {
    errno = EAGAIN;
    ret = -1;
    goto out;
  }

  /* allocate transmit buffer */
  ret = flextcp_connection_tx_alloc2(&s->data.connection.c, len, &dst_1, &len_1,
      &dst_2);
  if (ret < 0) {
    fprintf(stderr, "sendmsg: flextcp_connection_tx_alloc failed\n");
    abort();
  }

  if (ret == 0) {
    if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK) {
      errno = EAGAIN;
      ret = -1;
      goto out;
    } else {
      do {
        flextcp_sockctx_poll(ctx);

        ret = flextcp_connection_tx_alloc2(&s->data.connection.c, len, &dst_1,
            &len_1, &dst_2);
        if (ret < 0) {
          fprintf(stderr, "sendmsg: flextcp_connection_tx_alloc failed\n");
          abort();
        }
      } while (ret == 0);
    }
  }
  len_2 = ret - len_1;

  /* copy into TX buffer */
  memcpy(dst_1, buf, len_1);
  memcpy(dst_2, (const uint8_t *) buf + len_1, len_2);

  /* send out */
  /* TODO: this should not block for non-blocking sockets */
  while (flextcp_connection_tx_send(ctx, &s->data.connection.c, ret) != 0) {
    flextcp_sockctx_poll(ctx);
  }

out:
  flextcp_fd_release(sockfd);
  return ret;
}

/******************************************************************************/
/* map:
 *   - read, recv, recvfrom  -->  recvmsg
 *   - write, send, sendto  -->  sendmsg
 */

ssize_t _SF(read)(int sockfd, void *buf, size_t len)
{
  return recv_simple(sockfd, buf, len, 0);
}

ssize_t _SF(recv)(int sockfd, void *buf, size_t len, int flags)
{
  return recv_simple(sockfd, buf, len, flags);
}

ssize_t _SF(recvfrom)(int sockfd, void *buf, size_t len, int flags,
    struct sockaddr *src_addr, socklen_t *addrlen)
{
  ssize_t ret;

  ret = recv_simple(sockfd, buf, len, flags);

  if (src_addr != NULL) {
    *addrlen = *addrlen;
  }
  return ret;
}

ssize_t _SF(write)(int sockfd, const void *buf, size_t len)
{
  return send_simple(sockfd, buf, len, 0);
}

ssize_t _SF(send)(int sockfd, const void *buf, size_t len, int flags)
{
  return send_simple(sockfd, buf, len, flags);
}

ssize_t _SF(sendto)(int sockfd, const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, socklen_t addrlen)
{
  return send_simple(sockfd, buf, len, flags);
}
