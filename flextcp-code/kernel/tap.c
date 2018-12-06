/**************************************************************************
 * simpletun.c                                                            *
 *                                                                        *
 * A simplistic, simple-minded, naive tunnelling program using tun/tap    *
 * interfaces and TCP. DO NOT USE THIS PROGRAM FOR SERIOUS PURPOSES.      *
 *                                                                        *
 * You have been warned.                                                  *
 *                                                                        *
 * (C) 2010 Davide Brini.                                                 *
 *                                                                        *
 * DISCLAIMER AND WARNING: this is all work in progress. The code is      *
 * ugly, the algorithms are naive, error checking and input validation    *
 * are very basic, and of course there can be bugs. If that's not enough, *
 * the program has not been thoroughly tested, so it might even fail at   *
 * the few simple things it should be supposed to do right.               *
 * Needless to say, I take no responsibility whatsoever for what the      *
 * program might do. The program has been written mostly for learning     *
 * purposes, and can be used in the hope that is useful, but everything   *
 * is to be taken "as is" and without any kind of warranty, implicit or   *
 * explicit. See the file LICENSE for further details.                    *
 *************************************************************************/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h> 
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdarg.h>
#include <inttypes.h>
#include <sys/poll.h>


//based on: https://backreference.org/2010/03/26/tuntap-interface-tutorial/

#include "internal.h"

static int tap_fd;
static struct pollfd poll_fd;
static pthread_t tap_thread;

uint8_t TAP_MAC[6];
uint8_t TAP_MAC_NET[6];
/*
 * Returns 0 if successful, -err if error.
 */
int tap_init(uint32_t ip4)
{
    //this is the entry point of any TUN/TAP device
    //and it needs root or a network cap.
    if( (tap_fd = open("/dev/net/tun" , O_RDWR)) < 0 ) {
        perror("Can't open /dev/net/tun. Likely not being run as root.");
        return tap_fd;
    }

    //set ifreq struct
    struct ifreq ifr, ifr2;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strcpy(ifr.ifr_name, "tapdev");
    strcpy(ifr2.ifr_name, "tapdev");
    
    //create the tap device and get fd
    int err;
    if( (err = ioctl(tap_fd, TUNSETIFF, (void *)&ifr)) < 0 ) {
        perror("ioctl(TUNSETIFF)");
        close(tap_fd);
        return err;
    }


    /*
     * This is setting the TAP ip, might not be necessary
     * 
     * If you bind, you can bind to a specific IP address corresponding to one of the machine's interfaces, 
     * or you can bind to 0.0.0.0, in which case the socket will listen on all interfaces.
     * If you connect an unbound socket, then the machine's routing tables, in conjunction with the destination IP 
     * adress, will determine which interface the connection request goes out on.
     */ 

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Cannot open udp socket on TAP\n");
        close(tap_fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_addr.s_addr = ip4;
    addr.sin_family = AF_INET;
    memcpy( &ifr.ifr_addr, &addr, sizeof(struct sockaddr) );
    //memcpy( &ifr.ifr_hwaddr.sa_data, TAP_MAC, 6);

    if(ioctl(sock, SIOCSIFADDR, &ifr) < 0) {
        perror("ioctl: socket SIOCSIFADDR\n");
        close(tap_fd);
        return -1;
    }

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        perror("ioctl: socket SIOCGIFFLAGS\n");
        close(tap_fd);
        close(sock);
        return -1;
    }

    ifr.ifr_flags |= IFF_UP;
    ifr.ifr_flags |= IFF_RUNNING;

    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0)  {
        perror("ioctl: socket SIOCSIFFLAGS\n");
        close(tap_fd);
        close(sock);
        return -1;
    }

    if (ioctl(sock, SIOCGIFHWADDR, &ifr2) < 0)  {
        perror("ioctl: socket SIOCSIFFLAGS\n");
        close(tap_fd);
        close(sock);
        return -1;
    }

  
    close(sock);

    fprintf(stderr, "got MAC %hhX %hhX %hhX %hhX %hhX %hhX\n", ifr2.ifr_hwaddr.sa_data[0], ifr2.ifr_hwaddr.sa_data[1], ifr2.ifr_hwaddr.sa_data[2], ifr2.ifr_hwaddr.sa_data[3], ifr2.ifr_hwaddr.sa_data[4], ifr2.ifr_hwaddr.sa_data[5]);

    TAP_MAC[0] = ifr2.ifr_hwaddr.sa_data[0];
    TAP_MAC[1] = ifr2.ifr_hwaddr.sa_data[1];
    TAP_MAC[2] = ifr2.ifr_hwaddr.sa_data[2];
    TAP_MAC[3] = ifr2.ifr_hwaddr.sa_data[3];
    TAP_MAC[4] = ifr2.ifr_hwaddr.sa_data[4];
    TAP_MAC[5] = ifr2.ifr_hwaddr.sa_data[5];
    TAP_MAC_NET[0] = ifr2.ifr_hwaddr.sa_data[5];
    TAP_MAC_NET[1] = ifr2.ifr_hwaddr.sa_data[4];
    TAP_MAC_NET[2] = ifr2.ifr_hwaddr.sa_data[3];
    TAP_MAC_NET[3] = ifr2.ifr_hwaddr.sa_data[2];
    TAP_MAC_NET[4] = ifr2.ifr_hwaddr.sa_data[1];
    TAP_MAC_NET[5] = ifr2.ifr_hwaddr.sa_data[0];
    
    poll_fd.fd = tap_fd;
    poll_fd.events = POLLHUP | POLLIN;
    /*
     * https://stackoverflow.com/questions/17900459/how-to-set-ip-address-of-tun-device-and-set-link-up-through-c-program
     * https://linuxgazette.net/149/misc/melinte/udptun.c
     * https://stackoverflow.com/questions/36375530/what-is-the-destination-address-for-a-tap-tun-device
     */

    if(pthread_create(&tap_thread, NULL, tap_poll, NULL) != 0)
	    return -1;

    return 0; 
}


void* tap_poll(void* arg)
{
    uint8_t buf[1514];

    sleep(2);    
    while(1){ 
	    //fprintf(stderr, "polling tap\n");
    	    uint16_t size = poll(&poll_fd, 1, -1);
    	if(size > 0){
		int ret = tap_read(buf, 1514);
	    	assert(ret >= 0);
	    	//fprintf(stderr, "forwarding tap to network\n");
	    	//print_buf(buf, ret, 0);
		const struct pkt_tcp* p = (struct pkt_tcp *) buf;
		const struct eth_hdr *eth = (struct eth_hdr*) buf;
		const struct ip_hdr *ip = (struct ip_hdr *) (eth + 1);
		const struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);

		//PUT AWAY
		if (f_beui16(eth->type) == ETH_TYPE_ARP) {
		  if (ret < sizeof(struct pkt_arp)) {
		    fprintf(stderr, "tap process_packet: short arp packet\n");
		    //goto tap_err;
		    continue;
		    //return;
		  }
		  fprintf(stderr, "tap arp packet\n");
		  send_network_raw(buf, ret);

		} else if (f_beui16(eth->type) == ETH_TYPE_IP) {
		  if (ret < sizeof(*eth) + sizeof(*ip)) {
		    fprintf(stderr, "tap process_packet: short ip packet\n");
		    //goto tap_err;
		    continue;
		    //return;
		  }

		  if (ip->proto == IP_PROTO_TCP) {
		        if (ret < sizeof(*eth) + sizeof(*ip) + sizeof(*tcp)) {
			  fprintf(stderr, "tap process_packet: short tcp packet\n");
			  //goto tap_err;
			  continue;
			  //return;
		        }
			struct connection* conn = conn_lookup_rev(p);
		        fprintf(stderr, "tap tcp packet\n");

			if (conn && conn->status == CONN_REG_SYNACK) {
     				fprintf(stderr, "conn found\n");
				conn->local_seq = f_beui32(tcp->seqno) + 1; 
				conn->remote_seq = f_beui32(tcp->ackno);

				if ((ret = conn->comp.status) != 0 ||
      			    	   (ret = conn_reg_synack(conn)) != 0)
      				{
      			  		conn_failed(conn, ret);
      				}
			} else {
				fprintf(stderr, "conn %p not found\n", conn);
				send_network_raw(buf, ret);
			}

		      //tcp_packet(buf, len, fn_core, flow_group);
		    } else {
		    	fprintf(stderr, "tap ip, not tcp, packet\n");
		    	send_network_raw(buf, ret);
		    }
		}

	    } else {
	    	fprintf(stderr, "got nothing\n");
    	}
	//continue;

	//tap_err:
	//	fprintf(stderr, "tap read error\n");
    }
    return NULL;
}

/*
 * (proxy call to POSIX read; man read)
 * If there's data available, write it to the pointer passed, return value is number of bytes.
 * Please be sure to pass a buffer large enough. TODO: do we support jumbo frames?
 * If there's no data, return zero.
 * On error, return negative.
 */
int tap_read(uint8_t* buf, size_t count)
{
    return read(tap_fd, buf, count);
}

/*
 * (proxy call to POSIX write; man write)
 * Tries to write data to the tap device. n bytes from buf.
 * Returns bytes written if successful, negative if error.
 */
int tap_write(uint8_t* buf, size_t count) 
{
    //fprintf(stderr, "writing to tap %zu\n", count);
    memcpy(buf, TAP_MAC, 6);
    return write(tap_fd, buf, count);
}
