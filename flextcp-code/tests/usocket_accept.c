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

  while(1) {
    if ((connfd = lwip_accept(listenfd, NULL, NULL)) < 0) {
      perror("accept failed");
    }
    printf("Accepted connection\n");
  }
}
