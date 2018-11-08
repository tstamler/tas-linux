#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>
#include <dlfcn.h>
#include <utils.h>
#include <utils_timeout.h>

#include <flextcp_sockets.h>
#include "internal.h"

#define LINUX_POLL_DELAY 10

#define EPOLL_DEBUG(x...) do {} while (0)
//#define EPOLL_DEBUG(x...) fprintf(stderr, x)

static void libc_ptrs_init(void);
static inline void es_add_inactive(struct epoll_socket *es);
static inline void es_activate(struct epoll_socket *es);
static inline void es_deactivate(struct epoll_socket *es);
static inline void es_active_pushback(struct epoll_socket *es);
static inline void es_remove_ep(struct epoll_socket *es);
static inline void es_remove_sock(struct epoll_socket *es);
static inline uint64_t get_msecs(void);

static int (*libc_epoll_create1)(int flags) = NULL;
static int (*libc_epoll_ctl)(int epfd, int op, int fd,
    struct epoll_event *event) = NULL;
static int (*libc_epoll_wait)(int epfd, struct epoll_event *events,
			      int maxevents, int timeout) = NULL;
static int (*libc_close)(int fd);

int _SF(select)(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
		struct timeval *timeout)
{
  assert(!"NYI");
  errno = ENOTSUP;
  return -1;
}

int _SF(epoll_create)(int size)
{
  if (size <= 0) {
    errno = EINVAL;
    return -1;
  }
  return _SF(epoll_create1)(0);
}

int _SF(epoll_create1)(int flags)
{
  int fd;
  struct epoll *ep;

  libc_ptrs_init();

  if ((fd = libc_epoll_create1(flags)) == -1) {
    return -1;
  }

  if (flextcp_fd_ealloc(&ep, fd) < 0) {
    libc_close(fd);
    return -1;
  }

  ep->inactive = NULL;
  ep->active_first = NULL;
  ep->active_last = NULL;
  ep->num_linux = 0;
  ep->linux_cnt = 0;

  flextcp_fd_release(fd);
  return fd;
}

int _SF(epoll_ctl)(int epfd, int op, int fd, struct epoll_event *event)
{
  struct epoll *ep;
  struct socket *s;
  struct epoll_socket *es;
  int ret = 0;
  uint32_t em;

  libc_ptrs_init();
  EPOLL_DEBUG("flextcp_epoll_ctl(%d, %d, %d, {events=%x})\n", epfd, op,
      fd, (event != NULL ? event->events : -1));

  if (flextcp_fd_elookup(epfd, &ep) != 0) {
    errno = EBADF;
    return -1;
  }

  /* handle linux fds */
  if (flextcp_fd_slookup(fd, &s) != 0) {
    /* this is a linux fd */
    if ((ret = libc_epoll_ctl(epfd, op, fd, event)) != 0) {
      goto out;
    }

    /* make sure num_linux is accurate */
    if (op == EPOLL_CTL_ADD) {
      ep->num_linux++;
    } else if (op == EPOLL_CTL_DEL) {
      assert(ep->num_linux > 0);
      ep->num_linux--;
    }
    goto out;
  }

  /* look up socket on epoll */
  for (es = s->eps; es != NULL && es->ep != ep; es = es->so_next);

  /* validate events */
  if (op == EPOLL_CTL_ADD || op == EPOLL_CTL_MOD) {
    em = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP;
    event->events &= (~EPOLLET);	// XXX: Mask edge-triggered
    if ((event->events & (~em)) != 0) {
      fprintf(stderr, "flextcp epoll_ctl: unsupported events: %x\n",
          (event->events & (~em)));
      errno = EINVAL;
      ret = -1;
      goto out;
    }
  }

  /* execute operation */
  if (op == EPOLL_CTL_ADD) {
    /* add fd to epoll */
    if (es != NULL) {
      /* socket not on this epoll */
      errno = EEXIST;
      ret = -1;
      goto out;
    }

    /* allocate epoll_socket */
    if ((es = calloc(1, sizeof(*es))) == NULL) {
      errno = ENOMEM;
      ret = -1;
      goto out;
    }

    es->ep = ep;
    es->s = s;
    es->data = event->data;
    es->mask = event->events | EPOLLERR;
    es->active = 0;

    /* add to list on socket */
    es->so_prev = NULL;
    if (s->eps == NULL) {
      es->so_next = NULL;
      s->eps = es;
    } else {
      es->so_next = s->eps;
      s->eps->so_prev = es;
      s->eps = es;
    }

    /* add to inactive queue */
    es_add_inactive(es);

    /* check if this epoll is already active */
    if ((s->ep_events & es->mask) != 0) {
      es_activate(es);
    }
  } else if (op == EPOLL_CTL_MOD) {
    /* modify fd in epoll */

    if (es == NULL) {
      /* socket not on this epoll */
      errno = ENOENT;
      ret = -1;
      goto out;
    }

    es->mask = event->events | EPOLLERR;
    if ((s->ep_events & es->mask) != 0) {
      es_activate(es);
    }
  } else if (op == EPOLL_CTL_DEL) {
    /* remove fd from epoll */

    if (es == NULL) {
      /* socket not on this epoll */
      errno = ENOENT;
      ret = -1;
      goto out;
    }

    es_remove_sock(es);
    es_remove_ep(es);
    free(es);
  } else {
    /* unknown operation */
    errno = EINVAL;
    ret = -1;
    goto out;
  }
out:
  flextcp_fd_release(epfd);
  return ret;
}

/* #include <pthread.h> */

int _SF(epoll_wait)(int epfd, struct epoll_event *events, int maxevents,
    int timeout)
{
  struct flextcp_context *ctx;
  struct epoll *ep;
  struct epoll_socket *es;
  struct socket *s;
  int ret = 0, n = 0, nevents = 0;
  uint32_t i, num_active;
  uint64_t mtimeout = 0;

  libc_ptrs_init();
  EPOLL_DEBUG("flextcp_epoll_wait(%d, %d, %d)\n", epfd, maxevents, timeout);

  /* fprintf(stderr, "flextcp_epoll_wait(%d, %d, %d) on %p\n", epfd, maxevents, timeout, (void *)pthread_self()); */

  if (maxevents <= 0) {
    errno = EINVAL;
    return -1;
  }

  if (flextcp_fd_elookup(epfd, &ep) != 0) {
    errno = EBADF;
    return -1;
  }

  if(ep->num_linux > 0) {
    // XXX: Has Linux FDs - go straight to Linux
    return libc_epoll_wait(epfd, events, maxevents, timeout);
  }

  /* calculate timeout */
  if (timeout > 0) {
    mtimeout = get_msecs() + timeout;
  }

  ctx = flextcp_sockctx_get();

  do {
    /* check whether we have to give priority to linux fds */
    /* TODO */
    /*if (ep->num_linux > 0) {
      if (
    }*/

    /* make sure to poll for some events even if there is already enough on the
     * epoll */
    again:
    nevents = flextcp_sockctx_poll_n(ctx, maxevents);

    // Block thread if nothing received for a while
    static uint32_t __thread startwait = 0;
    if(UNLIKELY(nevents == 0)) {
      uint32_t cur_ts = util_timeout_time_us();

      if(startwait == 0) {
	startwait = cur_ts;
      } else if(cur_ts - startwait >= POLL_CYCLE) {
	// Idle -- wait for data from apps/flexnic
	flextcp_block(ctx, timeout);
	// Gotta check again now that we woke up
        startwait = 0;
	goto again;
      }
    } else {
      startwait = 0;
    }

    num_active = ep->num_active;
    for (i = 0; i < num_active && n < maxevents; i++) {
      es = ep->active_first;
      if (es == NULL) {
        /* no more active fds */
        break;
      }

      /* check whether fd is actually active */
      s = es->s;
      if ((s->ep_events & es->mask) != 0) {
        events[n].events = s->ep_events & es->mask;
        events[n].data = es->data;
        n++;
        es_active_pushback(es);
      } else {
        es_deactivate(es);
      }
    }
  } while (n == 0 && timeout != 0 && (timeout == -1 || mtimeout < get_msecs()));

  ret = n;
/*out:*/
  flextcp_fd_release(epfd);
  EPOLL_DEBUG("        = %d\n", ret);
  return ret;
}

int _SF(epoll_pwait)(int epfd, struct epoll_event *events, int maxevents,
    int timeout, const sigset_t *sigmask)
{
  fprintf(stderr, "flextcp epoll_pwait: TODO\n");
  errno = EINVAL;
  return -1;
}

void flextcp_epoll_sockinit(struct socket *s)
{
  s->ep_events = 0;
  s->eps = NULL;
#if 0
  s->eps_exc_first = NULL;
  s->eps_exc_last = NULL;
#endif
}

void flextcp_epoll_set(struct socket *s, uint32_t evts)
{
  uint32_t newevs;
  struct epoll_socket *es;

  newevs = (~s->ep_events) & evts;

  EPOLL_DEBUG("flextcp_epoll_set(%p, %x) ne=%x\n", s, evts, newevs);
  if (newevs == 0) {
    /* no new events */
    return;
  }
  s->ep_events |= evts;

  for (es = s->eps; es != NULL; es = es->so_next) {
    if ((newevs & es->mask) == 0) {
      continue;
    }

    es_activate(es);
  }
}

void flextcp_epoll_clear(struct socket *s, uint32_t evts)
{
  EPOLL_DEBUG("flextcp_epoll_clear(%p, %x)\n", s, evts);
  s->ep_events &= ~evts;
}

/* remove es from epoll's inactive list */
static inline void es_remove_inactive(struct epoll_socket *es)
{
  struct epoll *ep = es->ep;

  assert(es->active == 0);

  /* update predecessor's next pointer */
  if (es->ep_prev != NULL) {
    es->ep_prev->ep_next = es->ep_next;
  } else {
    ep->inactive = es->ep_next;
  }

  /* update successor's prev pointer */
  if (es->ep_next != NULL) {
    es->ep_next->ep_prev = es->ep_prev;
  }
}

static inline void es_add_inactive(struct epoll_socket *es)
{
  struct epoll *ep = es->ep;

  es->ep_prev = NULL;
  if (ep->inactive == NULL) {
    es->ep_next = NULL;
  } else {
    es->ep_next = ep->inactive;
    ep->inactive->ep_prev = es;
  }
  ep->inactive = es;
}

/* remove es from epoll's active list */
static inline void es_remove_active(struct epoll_socket *es)
{
  struct epoll *ep = es->ep;
  assert(es->active == 1);
  assert(ep->num_active > 0);

  /* update predecessor's next pointer */
  if (es->ep_prev != NULL) {
    es->ep_prev->ep_next = es->ep_next;
  } else {
    ep->active_first = es->ep_next;
  }

  /* update successor's prev pointer */
  if (es->ep_next != NULL) {
    es->ep_next->ep_prev = es->ep_prev;
  } else {
    ep->active_last = es->ep_prev;
  }

  ep->num_active--;
}

static inline void es_add_active(struct epoll_socket *es)
{
  struct epoll *ep = es->ep;

  if (ep->active_last == NULL) {
    es->ep_prev = NULL;
    es->ep_next = NULL;
    ep->active_first = es;
    ep->active_last = es;
  } else {
    es->ep_next = NULL;
    es->ep_prev = ep->active_last;
    ep->active_last->ep_next = es;
    ep->active_last = es;
  }

  ep->num_active++;
}

/* ensure es is active */
static inline void es_activate(struct epoll_socket *es)
{
  if (es->active != 0) {
    return;
  }

  /* remove from inactive list */
  es_remove_inactive(es);

  es->active = 1;

  /* add to end of active list */
  es_add_active(es);
}

static inline void es_deactivate(struct epoll_socket *es)
{
  if (es->active == 0) {
    return;
  }

  /* remove from active list */
  es_remove_active(es);

  es->active = 0;

  /* add to end of inactive list */
  es_add_inactive(es);
}

static inline void es_active_pushback(struct epoll_socket *es)
{
  assert(es->active != 0);
  es_remove_active(es);
  es_add_active(es);
}

/* remove es from epoll lists */
static inline void es_remove_ep(struct epoll_socket *es)
{
  if (es->active != 0) {
    es_remove_active(es);
  } else {
    es_remove_inactive(es);
  }
}

/* remove es from socket lists */
static inline void es_remove_sock(struct epoll_socket *es)
{
  struct socket *s = es->s;

  /* update predecessor's next pointer on socket list */
  if (es->so_prev == NULL) {
    s->eps = es->so_next;
  } else {
    es->so_prev->so_next = es->so_next;
  }

  /* update successor's prev pointer on socket list */
  if (es->so_next != NULL) {
    es->so_next->so_prev = es->so_prev;
  }
}

static inline uint64_t get_msecs(void)
{
  int ret;
  struct timespec ts;

  ret = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (ret != 0) {
    perror("flextcp get_msecs: clock_gettime failed\n");
    abort();
  }

  return ts.tv_sec * 1000ULL + (ts.tv_nsec / 1000000ULL);
}

static void libc_ptrs_init(void)
{
  void *handle;

  if (libc_epoll_create1 != NULL) {
    return;
  }

  if ((handle = dlopen("libc.so.6", RTLD_LAZY)) == NULL) {
    perror("flextcp epoll init dlopen on libc failed");
    abort();
  }
  if ((libc_epoll_create1 = dlsym(handle, "epoll_create1")) == NULL) {
    perror("flextcp init: dlsym epoll_create1 failed");
    abort();
  }
  if ((libc_epoll_ctl = dlsym(handle, "epoll_ctl")) == NULL) {
    perror("flextcp init: dlsym epoll_ctl failed");
    abort();
  }
  if ((libc_epoll_wait = dlsym(handle, "epoll_wait")) == NULL) {
    perror("flextcp init: dlsym epoll_wait failed");
    abort();
  }
  if ((libc_close = dlsym(handle, "close")) == NULL) {
    perror("flextcp init: dlsym close failed");
    abort();
  }
}
