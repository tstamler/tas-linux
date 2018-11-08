#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netdb.h>
#include <flextcp_sockets.h>

int main(int argc, char *argv[])
{
  int fd, ret, i;
  struct addrinfo hints;
  struct addrinfo *res;
  struct iovec iov[3];
  char buf1[1] = "H";
  char buf2[5] = "ello ";
  char buf3[7] = "World!\n";
  struct msghdr msg;

  if (argc != 3) {
    fprintf(stderr, "Usage: usocket_connect IP PORT\n");
    return -1;
  }

  lwip_init(0, NULL);

  /* parse address */
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(argv[1], argv[2], &hints, &res) != 0) {
    perror("getaddrinfo failed");
    return -1;
  }

  /* open socket */
  if ((fd = lwip_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket failed");
    abort();
  }

  /* connect socket */
  if (lwip_connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
    perror("connect failed");
    abort();
  }

  for (i = 0; i < 10; i++) {
    iov[0].iov_base = buf1;
    iov[0].iov_len = sizeof(buf1);
    iov[1].iov_base = buf2;
    iov[1].iov_len = sizeof(buf2);
    iov[2].iov_base = buf3;
    iov[2].iov_len = sizeof(buf3);
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;

    ret = lwip_sendmsg(fd, &msg, 0);
    if (ret < 0) {
      perror("sendmsg failed");
      abort();
    }
    if (ret != 13) {
      fprintf(stderr, "sendmsg returned %u (expected 13) :-/\n",
          (unsigned) ret);
      abort();
    }
  }

  lwip_close(fd);
  return 0;
}
