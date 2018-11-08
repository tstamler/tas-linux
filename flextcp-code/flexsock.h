/*
 * #defines to turn POSIX sockets into flexTCP sockets.
 *
 * Don't forget to call lwip_init() at program start.
 */

#ifndef FLEXSOCK_H
#define FLEXSOCK_H

#define socket		lwip_socket
#define connect		lwip_connect
#define send		lwip_send
#define sendto		lwip_sendto
#define sendmsg		lwip_sendmsg
#define recv		lwip_recv
#define recvfrom	lwip_recvfrom
#define bind		lwip_bind
#define listen		lwip_listen
#define setsockopt	lwip_setsockopt
#define accept		lwip_accept

/***** Not implemented *****/

static inline int lwip_setsockopt(int sockfd, int level, int optname,
				  const void *optval, socklen_t optlen)
{
  return 0;
}

#endif
