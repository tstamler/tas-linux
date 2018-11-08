#ifndef FLEXTCP_SOCKETS_H_
#define FLEXTCP_SOCKETS_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#define _SF(f) lwip_ ## f

void lwip_init(int argc, char *argv[]);


int _SF(socket)(int domain, int type, int protocol);

int _SF(close)(int sockfd);

int _SF(shutdown)(int sockfd, int how);

int _SF(bind)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

int _SF(connect)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

int _SF(listen)(int sockfd, int backlog);

int _SF(accept4)(int sockfd, struct sockaddr *addr, socklen_t *addrlen,
    int flags);

int _SF(accept)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);


int _SF(fcntl)(int sockfd, int cmd, ...);

int _SF(getsockopt)(int sockfd, int level, int optname, void *optval,
    socklen_t *optlen);

int _SF(setsockopt)(int sockfd, int level, int optname, const void *optval,
    socklen_t optlen);

int _SF(getsockname)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

int _SF(getpeername)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

int _SF(move_conn)(int sockfd);


ssize_t _SF(read)(int fd, void *buf, size_t count);

ssize_t _SF(recv)(int sockfd, void *buf, size_t len, int flags);

ssize_t _SF(recvfrom)(int sockfd, void *buf, size_t len, int flags,
    struct sockaddr *src_addr, socklen_t *addrlen);

ssize_t _SF(recvmsg)(int sockfd, struct msghdr *msg, int flags);


ssize_t _SF(write)(int fd, const void *buf, size_t count);

ssize_t _SF(send)(int sockfd, const void *buf, size_t len, int flags);

ssize_t _SF(sendto)(int sockfd, const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, socklen_t addrlen);

ssize_t _SF(sendmsg)(int sockfd, const struct msghdr *msg, int flags);


int _SF(epoll_create)(int size);

int _SF(epoll_create1)(int flags);

int _SF(epoll_ctl)(int epfd, int op, int fd, struct epoll_event *event);

int _SF(epoll_wait)(int epfd, struct epoll_event *events, int maxevents,
    int timeout);

int _SF(epoll_pwait)(int epfd, struct epoll_event *events, int maxevents,
    int timeout, const sigset_t *sigmask);

int _SF(select)(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
		struct timeval *timeout);

#endif /* ndef FLEXTCP_SOCKETS_H_ */
