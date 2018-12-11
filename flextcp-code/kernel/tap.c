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
#include <sys/epoll.h>

#include "internal.h"

static int tap_fd;
static int epoll_fd;
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

    //fprintf(stderr, "got MAC %hhX %hhX %hhX %hhX %hhX %hhX\n", ifr2.ifr_hwaddr.sa_data[0], ifr2.ifr_hwaddr.sa_data[1], ifr2.ifr_hwaddr.sa_data[2], ifr2.ifr_hwaddr.sa_data[3], ifr2.ifr_hwaddr.sa_data[4], ifr2.ifr_hwaddr.sa_data[5]);

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
    
    epoll_fd = epoll_create1(0);
    assert(epoll_fd > 0);
  
    if(pthread_create(&tap_thread, NULL, tap_poll, NULL) != 0)
	    return -1;

    return 0; 
}


void* tap_poll(void* arg)
{
    uint8_t buf[1514];
    struct epoll_event ev, events[10];
    int i, r;

    ev.events = EPOLLIN;
    ev.data.fd = tap_fd;

    //assert(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tap_fd, &ev) > 0);
    r = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tap_fd, &ev) > 0;
    if(r == -1){
	    perror("epoll ctl");
	    exit(1);
    }
    fprintf(stderr, "epoll ctl returned %d\n", r);


    sleep(2);    
    while(1){ 
	    //fprintf(stderr, "polling tap\n");
    	uint16_t nfds = epoll_wait(epoll_fd, events, 10, -1);
    	for(i=0; i < nfds; i++){
		int ret = tap_read(buf, 1514);
	    	assert(ret >= 0);
	    	//fprintf(stderr, "forwarding tap to network\n");
	    	//print_buf(buf, ret, 0);
		const struct pkt_tcp* p = (struct pkt_tcp *) buf;
		const struct eth_hdr *eth = (struct eth_hdr*) buf;
		const struct ip_hdr *ip = (struct ip_hdr *) (eth + 1);
		const struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);

		if (f_beui16(eth->type) == ETH_TYPE_ARP) {
		  if (ret < sizeof(struct pkt_arp)) {
		    fprintf(stderr, "tap process_packet: short arp packet\n");
		    //goto tap_err;
		    continue;
		    //return;
		  }
		  //fprintf(stderr, "tap arp packet\n");
		  //send_network_raw(buf, ret);
		  arp_packet_tap((uint8_t*) p, ret);

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
		    //    fprintf(stderr, "tap tcp packet\n");

			struct tcp_opts opts;
			r = parse_options(p, ret, &opts);
  			if (r != 0 || opts.ts == NULL) {
    				fprintf(stderr, "listener_packet: parsing options failed or no timestamp "
        					"option\n");
  			}

			if (conn && conn->status == CONN_REG_SYNACK) {
     				fprintf(stderr, "conn found\n");
				conn->linux_seq = f_beui32(tcp->seqno) + 1; 
				conn->remote_seq = f_beui32(tcp->ackno);
				conn->syn_ecr = f_beui32(opts.ts->ts_ecr);

				if ((ret = conn->comp.status) != 0 ||
      			    	   (ret = conn_reg_synack(conn)) != 0)
      				{
      			  		conn_failed(conn, ret);
      				}
			} else if(conn && (TCPH_FLAGS(&p->tcp) & (TCP_SYN | TCP_ACK)) 
						== (TCP_SYN | TCP_ACK)){
				send_control_tap_rev(conn, TCP_ACK, 1, conn->syn_ts, conn->syn_ecr, 0);
				fprintf(stderr, "resending ACK\n");
			} else if(conn) {
				fprintf(stderr, "I don't know what to do with this\n");
				print_buf(buf, ret, 0);
			} else	
			{

				//fprintf(stderr, "conn not found\n");
				send_network_raw(buf, ret);
			}

		      //tcp_packet(buf, len, fn_core, flow_group);
		    } else {
		    	//fprintf(stderr, "tap ip, not tcp, packet\n");
		    	send_network_raw(buf, ret);
		    }
		}

	}
	//continue;

	//tap_err:
	//	fprintf(stderr, "tap read error\n");
    }
    return NULL;
}

int tap_read(uint8_t* buf, size_t count)
{
    return read(tap_fd, buf, count);
}

int tap_write(uint8_t* buf, size_t count) 
{
    memcpy(buf, TAP_MAC, 6);
    return write(tap_fd, buf, count);
}
