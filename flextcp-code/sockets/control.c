#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <utils.h>
#include <flextcp_sockets.h>
#include <flextcp.h>

#include "internal.h"


void lwip_init(int argc, char *argv[])
{
  if (flextcp_fd_init() != 0) {
    fprintf(stderr, "flextcp_fd_init failed\n");
    abort();
  }

  if (flextcp_init() != 0) {
    fprintf(stderr, "flextcp_init failed\n");
    abort();
  }
}

int _SF(socket)(int domain, int type, int protocol)
{
  struct socket *s;
  int fd;
  int nonblock = 0;

  if ((type & SOCK_NONBLOCK) == SOCK_NONBLOCK) {
    nonblock = 1;
  }

  type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (domain != AF_INET || type != SOCK_STREAM) {
    errno = EINVAL;
    return -1;
  }

  if ((fd = flextcp_fd_salloc(&s)) < 0) {
    return -1;
  }

  s->type = SOCK_SOCKET;
  s->flags = 0;
  flextcp_epoll_sockinit(s);

  if (nonblock) {
    fprintf(stderr, "socket set to nonblock\n");
    s->flags |= SOF_NONBLOCK;
  }

  return fd;
}

int _SF(close)(int sockfd)
{
  struct socket *s;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  flextcp_fd_close(sockfd);

  fprintf(stderr, "flextcp close: TODO\n");
  errno = EINVAL;
  return -1;
}

int _SF(shutdown)(int sockfd, int how)
{
  struct socket *s;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  fprintf(stderr, "flextcp shutdown: TODO\n");
  errno = EINVAL;
  flextcp_fd_release(sockfd);
  return -1;
}

int _SF(bind)(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
  struct socket *s;
  int ret = 0;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  if (addrlen != sizeof(s->addr) || addr->sa_family != AF_INET) {
    errno = EINVAL;
    ret = -1;
    goto out;
  }

  memcpy(&s->addr, addr, sizeof(s->addr));
  s->flags |= SOF_BOUND;

out:
  flextcp_fd_release(sockfd);
  return ret;
}

int _SF(connect)(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
  struct socket *s;
  int ret = 0;
  struct sockaddr_in *sin = (struct sockaddr_in *) addr;
  struct flextcp_context *ctx;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  /* socket already used */
  if (s->type == SOCK_LISTENER ||
      (s->type == SOCK_CONNECTION && s->data.connection.status == SOC_CONNECTED))
  {
    errno = EISCONN;
    ret = -1;
    goto out;
  }

  /* non blocking socket connecting */
  if (s->type == SOCK_CONNECTION && s->data.connection.status == SOC_CONNECTING) {
    errno = EALREADY;
    ret = -1;
    goto out;
  }

  /* filter out invalid address types */
  if (addrlen != sizeof(s->addr) || addr->sa_family != AF_INET) {
    errno = EINVAL;
    ret = -1;
    goto out;
  }

  /* open flextcp connection */
  ctx = flextcp_sockctx_get();
  if (flextcp_connection_open(ctx, &s->data.connection.c,
        ntohl(sin->sin_addr.s_addr), ntohs(sin->sin_port)))
  {
    /* TODO */
    errno = ECONNREFUSED;
    ret = -1;
    goto out;
  }

  assert(s->type == SOCK_CONNECTION || s->type == SOCK_SOCKET);
  s->type = SOCK_CONNECTION;
  s->data.connection.status = SOC_CONNECTING;
  s->data.connection.listener = NULL;
  s->data.connection.rx_len_1 = 0;
  s->data.connection.rx_len_2 = 0;
  s->data.connection.ctx = ctx;

  /* check whether the socket is blocking */
  if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK) {
    /* if non-blocking, just return */
    errno = EINPROGRESS;
    ret = -1;
    goto out;
  } else {
    /* if this is blocking, wait for connection to complete */
    do {
      flextcp_sockctx_poll(ctx);
    } while (s->data.connection.status == SOC_CONNECTING);
  }

  if (s->data.connection.status == SOC_FAILED) {
    /* TODO */
    errno = ECONNREFUSED;
    ret = -1;
    goto out;
  }

out:
  flextcp_fd_release(sockfd);
  return ret;
}

int _SF(listen)(int sockfd, int backlog)
{
  struct socket *s;
  struct flextcp_context *ctx;
  int ret = 0;
  uint32_t flags = 0;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  /* socket already used */
  if (s->type != SOCK_SOCKET) {
    errno = EOPNOTSUPP;
    ret = -1;
    goto out;
  }

  /* socket not bound */
  /* TODO: technically sohuld probably bind to an ephemeral port */
  if ((s->flags & SOF_BOUND) != SOF_BOUND) {
    errno = EADDRINUSE;
    ret = -1;
    goto out;
  }

  /* pass on reuseport flags */
  if ((s->flags & SOF_REUSEPORT) == SOF_REUSEPORT) {
    flags |= FLEXTCP_LISTEN_REUSEPORT;
  }

  /* make sure we have a reasonable backlog */
  if (backlog < 8) {
    backlog = 8;
  }

  /* open flextcp listener */
  ctx = flextcp_sockctx_get();
  if (flextcp_listen_open(ctx, &s->data.listener.l, ntohs(s->addr.sin_port),
        backlog, flags))
  {
    /* TODO */
    errno = ECONNREFUSED;
    ret = -1;
    goto out;
  }

  s->type = SOCK_LISTENER;
  s->data.listener.backlog = backlog;
  s->data.listener.status = SOL_OPENING;
  s->data.listener.pending = NULL;

  /* wait for listen to complete */
  do {
    flextcp_sockctx_poll(ctx);
  } while (s->data.listener.status == SOL_OPENING);

  /* check whether listen failed */
  if (s->data.listener.status == SOL_FAILED) {
    /* TODO */
    errno = ENOBUFS;
    ret = -1;
    goto out;
  }

out:
  flextcp_fd_release(sockfd);
  return ret;
}

int _SF(accept4)(int sockfd, struct sockaddr *addr, socklen_t *addrlen,
    int flags)
{
  struct socket *s, *ns;
  struct flextcp_context *ctx;
  struct socket_pending *sp, *spp;
  int ret = 0, nonblock = 0, newfd;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  /* validate flags */
  if ((flags & SOCK_NONBLOCK) == SOCK_NONBLOCK) {
    nonblock = 1;
  }

  flags &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (flags != 0) {
    errno = EINVAL;
    ret = -1;
    goto out;
  }

  /* socket is not a listening socket */
  if (s->type != SOCK_LISTENER) {
    errno = EOPNOTSUPP;
    ret = -1;
    goto out;
  }

  ctx = flextcp_sockctx_get();

  /* lookup pending connection for this context/thread */
  for (sp = s->data.listener.pending; sp != NULL; sp = sp->next) {
    if (sp->ctx == ctx) {
      break;
    }
  }

  /* if there is no pending request, send out a request */
  if (sp == NULL) {
    if ((sp = malloc(sizeof(*sp))) == NULL) {
      errno = ENOMEM;
      ret = -1;
      goto out;
    }

    /* allocate socket structure */
    if ((newfd = flextcp_fd_salloc(&ns)) < 0) {
      free(sp);
      ret = -1;
      goto out;
    }

    ns->type = SOCK_CONNECTION;
    ns->flags = (nonblock ? SOF_NONBLOCK : 0);
    ns->data.connection.status = SOC_CONNECTING;
    ns->data.connection.listener = s;
    ns->data.connection.rx_len_1 = 0;
    ns->data.connection.rx_len_2 = 0;
    ns->data.connection.ctx = ctx;

    sp->fd = newfd;
    sp->s = ns;
    sp->ctx = ctx;
    sp->next = NULL;

    /* send accept request to kernel */
    if (flextcp_listen_accept(ctx, &s->data.listener.l,
          &ns->data.connection.c) != 0)
    {
      /* TODO */
      errno = ENOBUFS;
      ret = -1;
      free(sp);
      flextcp_fd_close(newfd);
      goto out;
    }

    /* append entry to pending list */
    spp = s->data.listener.pending;
    if (spp == NULL) {
      s->data.listener.pending = sp;
    } else {
      while (spp->next != NULL) {
        spp = spp->next;
      }
      spp->next = sp;
    }
  } else {
    ns = sp->s;
    newfd = sp->fd;
  }

  /* check if connection is still pending */
  if (ns->data.connection.status == SOC_CONNECTING) {
    flextcp_epoll_clear(s, EPOLLIN);
    if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK) {
      /* if non-blocking, just return */
      errno = EAGAIN;
      ret = -1;
      goto out;
    } else {
      /* if this is blocking, wait for connection to complete */
      do {
        flextcp_sockctx_poll(ctx);
      } while (ns->data.connection.status == SOC_CONNECTING);
    }
  }

  /* connection is opened now */
  assert(ns->data.connection.status == SOC_CONNECTED);

  /* remove entry from pending list */
  if (s->data.listener.pending == sp) {
    s->data.listener.pending = sp->next;
  } else {
    spp = s->data.listener.pending;
    assert(spp != NULL);
    while (spp->next != sp) {
      assert(spp->next != NULL);
      spp = spp->next;
    }
    spp->next = sp->next;
  }
  free(sp);
  flextcp_fd_release(newfd);

  // fill in addr if given
  if(addr != NULL) {
    int r = lwip_getpeername(newfd, addr, addrlen);
    assert(r == 0);
  }

  ret = newfd;
out:
  flextcp_fd_release(sockfd);
  return ret;
}

/** map: accept  -->  accept4 */
int _SF(accept)(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  return _SF(accept4)(sockfd, addr, addrlen, 0);
}

int _SF(fcntl)(int sockfd, int cmd, ...)
{
  struct socket *s;
  int ret = 0;
  int iarg;
  va_list arg;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  switch (cmd) {
    case F_GETFL:
      /* return flags */
      if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK) {
        ret |= O_NONBLOCK;
      }
      break;

    case F_SETFL:
      /* update flags */
      va_start(arg, cmd);
      iarg = va_arg(arg, int);
      va_end(arg);

      /* make sure only supported flags are set */
      if ((iarg & ~O_NONBLOCK) != 0) {
        fprintf(stderr, "flextcp fcntl: unsupported flags set (%x)\n",
            iarg);
        /* not sure if that's the right error code */
        errno = EINVAL;
        ret = -1;
        goto out;
      }

      /* set or clear nonblocking socket flags */
      if ((iarg & O_NONBLOCK) == 0) {
        s->flags &= ~SOF_NONBLOCK;
      } else {
        s->flags |= SOF_NONBLOCK;
      }
      break;

    default:
      fprintf(stderr, "flextcp fcntl: unsupported cmd\n");
      errno = EINVAL;
      ret = -1;
      goto out;
  }

out:
  flextcp_fd_release(sockfd);
  return ret;
}

int _SF(getsockopt)(int sockfd, int level, int optname, void *optval,
    socklen_t *optlen)
{
  struct socket *s;
  int ret = 0, res, len;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  if(level == IPPROTO_TCP && optname == TCP_NODELAY) {
    /* check nodelay flag: always set */
    res = 1;

  } else if(level == SOL_SOCKET &&
      (optname == SO_RCVBUF || optname == SO_SNDBUF))
  {
    /* read receive or transmit buffer size: TODO */
    res = 1024 * 1024;
  } else if (level == SOL_SOCKET && optname == SO_ERROR) {
    /* check socket error */
    if (s->type == SOCK_LISTENER) {
      res = (s->data.listener.status == SOL_OPEN ? 0 : EINPROGRESS);
    } else if (s->type == SOCK_CONNECTION) {
      if (s->data.connection.status == SOC_CONNECTED) {
        res = 0;
      } else if (s->data.connection.status == SOC_CONNECTING) {
        res = EINPROGRESS;
      } else {
        /* TODO */
        res = ECONNREFUSED;
      }
    } else {
      /* TODO */
      res = ENOTSUP;
    }
  } else if (level == SOL_SOCKET && optname == SO_REUSEPORT) {
    res = !!(s->flags & SOF_REUSEPORT);
  } else {
    /* unknown option */
    fprintf(stderr, "flextcp getsockopt: unknown level optname combination "
        "(l=%u, o=%u)\n", level, optname);
    errno = ENOPROTOOPT;
    ret = -1;
    goto out;
  }

  /* copy result to optval, truncate if necessary */
  len = MIN(*optlen, sizeof(res));
  memcpy(optval, &res, len);
  *optlen = res;

out:
  flextcp_fd_release(sockfd);
  return ret;
}

int _SF(setsockopt)(int sockfd, int level, int optname, const void *optval,
    socklen_t optlen)
{
  struct socket *s;
  int ret = 0, res;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  if(level == IPPROTO_TCP && optname == TCP_NODELAY) {
    /* do nothing */
    if (optlen != sizeof(int)) {
      errno = EINVAL;
      ret = -1;
      goto out;
    }
  } else if(level == SOL_SOCKET &&
      (optname == SO_RCVBUF || optname == SO_SNDBUF))
  {
    if (optlen < sizeof(int)) {
      errno = EINVAL;
      ret = -1;
      goto out;
    }

    /* we allow "resizing" up to 1MB */
    res = * ((int *) optval);
    if (res <= 1024 * 1024) {
      return 0;
    } else {
      errno = ENOMEM;
      ret = -1;
      goto out;
    }
  } else if (level == SOL_SOCKET && optname == SO_REUSEPORT) {
    if (optlen != sizeof(int)) {
      errno = EINVAL;
      ret = -1;
      goto out;
    }

    if (*(int *) optval != 0) {
      s->flags |= SOF_REUSEPORT;
    } else {
      s->flags &= ~SOF_REUSEPORT;
    }
  } else if (level == SOL_SOCKET && optname == SO_REUSEADDR) {
    fprintf(stderr, "flextcp setsockopt: Ignoring REUSEADDR\n");
    // Ignore...
  } else {
    /* unknown option */
    fprintf(stderr, "flextcp setsockopt: unknown level optname combination "
        "(l=%u, o=%u)\n", level, optname);
    errno = ENOPROTOOPT;
    ret = -1;
    goto out;
  }

out:
  flextcp_fd_release(sockfd);
  return ret;
}

int _SF(getsockname)(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  struct socket *s;
  int ret = 0;
  socklen_t len;
  struct sockaddr_in sin;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  if ((s->flags & SOF_BOUND) == SOF_BOUND) {
    sin = s->addr;
  } else if (s->type == SOCK_CONNECTION &&
      s->data.connection.status == SOC_CONNECTED)
  {
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    /* FIXME: without breaking abstraction */
    sin.sin_addr.s_addr = htonl(s->data.connection.c.local_ip);
    sin.sin_port = htons(s->data.connection.c.local_port);
  } else {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  len = MIN(*addrlen, sizeof(sin));
  *addrlen = len;
  memcpy(addr, &sin, len);

out:
  flextcp_fd_release(sockfd);
  return ret;
}

int _SF(getpeername)(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  struct socket *s;
  int ret = 0;
  socklen_t len;
  struct sockaddr_in sin;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  /* if not connection or not currently connected then there is no peername */
  if (s->type != SOCK_CONNECTION ||
      s->data.connection.status != SOC_CONNECTED)
  {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  /* FIXME: without breaking abstraction */
  sin.sin_addr.s_addr = htonl(s->data.connection.c.remote_ip);
  sin.sin_port = htons(s->data.connection.c.remote_port);

  len = MIN(*addrlen, sizeof(sin));
  *addrlen = len;
  memcpy(addr, &sin,  len);

out:
  flextcp_fd_release(sockfd);
  return ret;
}

/* #include <pthread.h> */

int _SF(move_conn)(int sockfd)
{
  struct socket *s;
  int ret = 0;
  struct flextcp_context *ctx;

  /* fprintf(stderr, "%s: lwip_move_conn(%d) called on thread %p\n", HOSTNAME, sockfd, (void *)pthread_self()); */

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  /* if not connection or not currently connected then there is no peername */
  if (s->type != SOCK_CONNECTION ||
      s->data.connection.status != SOC_CONNECTED)
  {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  ctx = flextcp_sockctx_get();
  if (s->data.connection.ctx == ctx) {
    errno = EISCONN;
    ret = -1;
    goto out;
  }

  s->data.connection.move_status = INT_MIN;
  if (flextcp_connection_move(ctx, &s->data.connection.c) != 0) {
    /* TODO */
    errno = EINVAL;
    ret = -1;
    goto out;
  }

  do {
    flextcp_sockctx_poll(ctx);
  } while (s->data.connection.move_status == INT_MIN);
  ret = s->data.connection.move_status;
  if (ret == 0) {
    s->data.connection.ctx = ctx;
  }

out:
  flextcp_fd_release(sockfd);
  return ret;
}