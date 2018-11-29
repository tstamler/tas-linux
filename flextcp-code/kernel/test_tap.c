#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <net/route.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <unistd.h>
#include "tap.h"

/*
 *  just a quick and dirty test
 */ 

int main()
{ 
    uint32_t local_ip4 = 0L; 
   
    if (inet_pton(AF_INET, "12.12.12.12", &local_ip4)) {
        printf("pton failed\n"); 
        return 1;
    }

    int err = tap_init(local_ip4);
    if (err) {
        printf("tap init didnt work, err: %d\n", err);
    }

    char* buf[2000];
    int rd = tap_read(buf, 2000);

    printf("return of read:  %d\n", rd);

    return 0;
}

