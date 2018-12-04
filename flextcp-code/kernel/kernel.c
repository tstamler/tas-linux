#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <errno.h>

#include <utils.h>
#include "internal.h"

static void timeout_trigger(struct timeout *to, uint8_t type, void *opaque);
void flexnic_loadmon(uint32_t cur_ts);

struct timeout_manager timeout_mgr;
static int exited = 0;
struct configuration config;
struct kernel_statistics kstats;
uint32_t cur_ts;
static uint32_t startwait = 0;
int kernel_notifyfd = 0;

#ifdef SPLITTCP
int kernel_main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
  uint32_t last_print = 0;
  uint32_t loadmon_ts = 0;

  kernel_notifyfd = eventfd(0, 0);
  assert(kernel_notifyfd != -1);

  struct epoll_event ev = {
    .events = EPOLLIN,
    .data.fd = kernel_notifyfd,
  };

  int epfd = epoll_create1(0);
  assert(epfd != -1);

  int r = epoll_ctl(epfd, EPOLL_CTL_ADD, kernel_notifyfd, &ev);
  assert(r == 0);

  if (config_parse(&config, argc, argv) != 0) {
    return EXIT_FAILURE;
  }

  if (tap_init(htonl(config.ip))) {
    fprintf(stderr, "tap_init failed\n");
    return EXIT_FAILURE;
  }

  /* initialize timers for timeouts */
  if (util_timeout_init(&timeout_mgr, timeout_trigger, NULL)) {
    fprintf(stderr, "timeout_init failed\n");
    return EXIT_FAILURE;
  }

  /* initialize routing subsystem */
  if (routing_init()) {
    fprintf(stderr, "routing_init failed\n");
    return EXIT_FAILURE;
  }

  /* connect to NIC */
  if (nicif_init()) {
    fprintf(stderr, "nicif_init failed\n");
    return EXIT_FAILURE;
  }

  /* initialize CC */
  if (cc_init()) {
    fprintf(stderr, "cc_init failed\n");
    return EXIT_FAILURE;
  }

  /* prepare application interface */
  if (appif_init()) {
    fprintf(stderr, "appif_init failed\n");
    return EXIT_FAILURE;
  }

  if (arp_init()) {
    fprintf(stderr, "arp_init failed\n");
    return EXIT_FAILURE;
  }


  if (tcp_init()) {
    fprintf(stderr, "tcp_init failed\n");
    return EXIT_FAILURE;
  }

  printf("kernel ready\n");
  fflush(stdout);

  while (exited == 0) {
    unsigned n = 0;

    cur_ts = util_timeout_time_us();
    n += nicif_poll();
    n += cc_poll(cur_ts);
    n += appif_poll();
    n += tap_poll();
    tcp_poll();
    util_timeout_poll_ts(&timeout_mgr, cur_ts);

    if (cur_ts - loadmon_ts >= 10000) {
      flexnic_loadmon(cur_ts);
      loadmon_ts = cur_ts;
    }

    if(UNLIKELY(n == 0)) {
      if(startwait == 0) {
	startwait = cur_ts;
      } else if(cur_ts - startwait >= POLL_CYCLE) {
	// Idle -- wait for data from apps/flexnic
	uint32_t cc_timeout = cc_next_ts(cur_ts),
	  util_timeout = util_timeout_next(&timeout_mgr, cur_ts),
	  timeout_us;
	int timeout_ms;

	if(cc_timeout != -1U && util_timeout != -1U) {
	  timeout_us = MIN(cc_timeout, util_timeout);
	} else if(cc_timeout != -1U) {
	  timeout_us = util_timeout;
	} else {
	  timeout_us = cc_timeout;
	}
	if(timeout_us != -1U) {
	  timeout_ms = timeout_us / 1000;
	} else {
	  timeout_ms = -1;
	}

	// Deal with load management
	if(timeout_ms == -1 || timeout_ms > 1000) {
	  timeout_ms = 10;
	}

	/* fprintf(stderr, "idle - timeout %d ms, cc_timeout = %u us, util_timeout = %u us\n", timeout_ms, cc_timeout, util_timeout); */
	struct epoll_event event[2];
	int n;
      again:
	n = epoll_wait(epfd, event, 2, timeout_ms);
	if(n == -1) {
	  if(errno == EINTR) {
	    // XXX: To support attaching GDB
	    goto again;
	  }
	}
	assert(n != -1);
	/* fprintf(stderr, "busy - %u events\n", n); */
	for(int i = 0; i < n; i++) {
	  assert(event[i].data.fd == kernel_notifyfd);
	  uint64_t val;
	  /* fprintf(stderr, "- woken up by event FD = %d\n", event[i].data.fd); */
	  int r = read(kernel_notifyfd, &val, sizeof(uint64_t));
	  assert(r == sizeof(uint64_t));
	}
      }
    } else {
      startwait = 0;
    }

    if (cur_ts - last_print >= 1000000) {
      printf("stats: drops=%"PRIu64" k_rexmit=%"PRIu64" ecn=%"PRIu64" acks=%"
          PRIu64"\n", kstats.drops, kstats.kernel_rexmit, kstats.ecn_marked,
          kstats.acks);
      fflush(stdout);
      last_print = cur_ts;
    }
  }

  return EXIT_SUCCESS;
}

static void timeout_trigger(struct timeout *to, uint8_t type, void *opaque)
{
  switch (type) {
    case TO_ARP_REQ:
      arp_timeout(to, type);
      break;

    case TO_TCP_HANDSHAKE:
    case TO_TCP_RETRANSMIT:
      tcp_timeout(to, type);
      break;

    default:
      fprintf(stderr, "Unknown timeout type: %u\n", type);
      abort();
  }
}
