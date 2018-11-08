#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <utils.h>
#include <unistd.h>
#include <sys/eventfd.h>

#include "internal.h"

#define MAXSOCK 128 * 1024

enum fh_type {
  FH_UNUSED,
  FH_SOCKET,
  FH_EPOLL,
};

struct filehandle {
  union {
    struct socket *s;
    struct epoll *e;
  } data;
  uint8_t type;
};

static struct filehandle fhs[MAXSOCK];

int flextcp_fd_init(void)
{
  return 0;
}


int flextcp_fd_salloc(struct socket **ps)
{
  struct socket *s;
  int fd;

  if ((s = calloc(1, sizeof(*s))) == NULL) {
    errno = ENOMEM;
    return -1;
  }

  /* get eventfd so we reserve the FD in the kernel to avoid overlap */
  if ((fd = eventfd(0, 0)) < 0) {
    free(s);
    return -1;
  }

  /* no more file handles available */
  if (fd >= MAXSOCK) {
    free(s);
    /* TODO: enusure this is the libc close */
    close(fd);
    errno = EMFILE;
    return -1;
  }

  s->type = SOCK_SOCKET;
  fhs[fd].data.s = s;
  fhs[fd].type = FH_SOCKET;

  *ps = s;

  return fd;
}

int flextcp_fd_slookup(int fd, struct socket **ps)
{
  if (fd >= MAXSOCK || fhs[fd].type != FH_SOCKET) {
    errno = EBADF;
    return -1;
  }

  *ps = fhs[fd].data.s;
  return 0;
}

int flextcp_fd_ealloc(struct epoll **pe, int fd)
{
  struct epoll *e;

  /* no more file handles available */
  if (fd >= MAXSOCK) {
    errno = EMFILE;
    return -1;
  }

  assert(fhs[fd].type == FH_UNUSED);

  if ((e = calloc(1, sizeof(*e))) == NULL) {
    errno = ENOMEM;
    return -1;
  }

  fhs[fd].data.e = e;
  fhs[fd].type = FH_EPOLL;

  *pe = e;

  return fd;
}

int flextcp_fd_elookup(int fd, struct epoll **pe)
{
  if (fd >= MAXSOCK || fhs[fd].type != FH_EPOLL) {
    errno = EBADF;
    return -1;
  }

  *pe = fhs[fd].data.e;
  return 0;
}

void flextcp_fd_release(int fd)
{
}

void flextcp_fd_close(int fd)
{
  struct socket *s = fhs[fd].data.s;
  fhs[fd].data.s = NULL;
  fhs[fd].type = FH_UNUSED;
  MEM_BARRIER();
  /* TODO: enusure this is the libc close */
  close(fd);
  free(s);
}
