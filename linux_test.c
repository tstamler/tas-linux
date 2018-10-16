#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <rte_config.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_launch.h>

static int parse_params(int argc, char *argv[]);
static int network_init();
static int tap_init();
static int poll_net();
static int poll_tap();

static const uint8_t port_id = 0;
static const struct rte_eth_conf port_conf = { 
    .rxmode = { 
      .split_hdr_size = 0,
      .header_split   = 0, /* Header Split disabled */
      .hw_ip_checksum = 0,
      .hw_vlan_filter = 0, /* VLAN filtering disabled */
      .jumbo_frame    = 0, /* Jumbo Frame Support disabled */
      .hw_strip_crc   = 0, /* CRC stripped by hardware */
      .mq_mode = ETH_MQ_RX_RSS,
    },  
    .txmode = { 
      .mq_mode = ETH_MQ_TX_NONE,
    },  
    .rx_adv_conf = { 
      .rss_conf = { 
        .rss_hf = ETH_RSS_TCP,
      },  
    }   
  }; 

static struct rte_eth_dev_info eth_devinfo;
struct ether_addr eth_addr;

int main(int argc, char *argv[])
{
    int res = EXIT_SUCCESS;

    if (parse_params(argc, argv) != 0) {
        res = EXIT_FAILURE;
        goto error_exit;
    }

    if (network_init() != 0) {
        res = EXIT_FAILURE;
        goto error_exit;
    }

    if (tap_init() != 0) {
        res = EXIT_FAILURE;
        goto error_exit;
    }

    printf("initialization done\n");
    fflush(stdout);

    while (1) {
        poll_net();
        poll_linux();
    }

    error_exit:
    return res;
}

static int parse_params(int argc, char *argv[])
{
    int n;
    char *end;

    /* initialize dpdk */
    if ((n = rte_eal_init(argc, argv)) < 0) {
        goto error_exit;
    }
  
    return 0;

    error_exit:
        fprintf(stderr, "DPDK initialization error\n");
        return -1;
}

static int network_init()
{
    int num_rx = num_tx = 1;  

/* make sure there is only one port */
    count = rte_eth_dev_count();
    if (count == 0) {
        fprintf(stderr, "No ethernet devices\n");
        goto error_exit;
    } else if (count > 1) {
        fprintf(stderr, "Multiple ethernet devices\n");
        goto error_exit;
    }

    /* initialize port */
    ret = rte_eth_dev_configure(port_id, num_rx, num_tx, &port_conf);
    if (ret < 0) {
        fprintf(stderr, "rte_eth_dev_configure failed\n");
        goto error_exit;
    }

    /* get mac address and device info */
    rte_eth_macaddr_get(port_id, &eth_addr);
    rte_eth_dev_info_get(port_id, &eth_devinfo);
    eth_devinfo.default_txconf.txq_flags = ETH_TXQ_FLAGS_NOVLANOFFL;

    return 0;

    error_exit:
        fprintf(stderr, "network_init failed\n");
        return -1;
}

static int tap_init(){
    //TODO: this
    // some combination of open and ioctl, I think
}

static int poll_net(){

    //TODO: 1) try to read packets from dpdk
    //      2) extract buffers from mbufs
    //      3) write buffers to tap

}

static int poll_tap(){

    //TODO: 1) try to read from tap
    //      2) convert buffers to dpdk mbufs
    //      3) send mbufs out over dpdk

}
