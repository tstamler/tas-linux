#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <flextcp_sockets.h>

int main(int argc, char *argv[])
{
  int listenfd = 0, connfd = 0;
  struct sockaddr_in serv_addr;
  ssize_t ret;
  struct iovec iov[3];
  char buf1[4];
  char buf2[4];
  char buf3[4];
  struct msghdr msg;

  lwip_init(0, NULL);
  if ((listenfd = lwip_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket failed");
    abort();
  }

  memset(&serv_addr, '0', sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(1234);

  if (lwip_bind(listenfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
    perror("bind failed");
    abort();
  }

  if (lwip_listen(listenfd, 10)) {
    perror("listen failed");
    abort();
  }

  if ((connfd = lwip_accept(listenfd, NULL, NULL)) < 0) {
    perror("accept failed");
    abort();
  }

  while(1) {
    iov[0].iov_base = buf1;
    iov[0].iov_len = sizeof(buf1);
    iov[1].iov_base = buf2;
    iov[1].iov_len = sizeof(buf2);
    iov[2].iov_base = buf3;
    iov[2].iov_len = sizeof(buf3);
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;

    ret = lwip_recvmsg(connfd, &msg, 0);
    if (ret < 0) {
      perror("recvmsg failed");
      abort();
    }
    if (ret == 0) {
      fprintf(stderr, "recvmsg returned 0 :-/\n");
      abort();
    }
    if (ret > 8) {
      printf("%.*s%.*s%.*s", 4, buf1, 4, buf2, (int) (ret - 8), buf3);
    } else if (ret > 4) {
      printf("%.*s%.*s", 4, buf1, (int) (ret - 4), buf2);
    } else {
      printf("%.*s", (int) ret, buf1);
    }
  }
}
