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

//based on: https://backreference.org/2010/03/26/tuntap-interface-tutorial/

static int tap_fd;

/*
 * Returns 0 if successful, -err if error.
 */
int tap_init()
{
    //this is the entry point of any TUN/TAP device
    //and it needs root or a network cap.
    if( (tap_fd = open("/dev/net/tun" , O_RDWR)) < 0 ) {
        perror("Can't open /dev/net/tun. Likely not being run as root.");
        return tap_fd;
    }

    //set ifreq struct
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strcpy(ifr.ifr_name, "tapdev");
    
    //create the tap device and get fd
    int err;
    if( (err = ioctl(tap_fd, TUNSETIFF, (void *)&ifr)) < 0 ) {
        perror("ioctl(TUNSETIFF)");
        close(tap_fd);
        return err;
    }

    return 0;
}

/*
 * (proxy call to POSIX read; man read)
 * If there's data available, write it to the pointer passed, return value is number of bytes.
 * Please be sure to pass a buffer large enough. TODO: do we support jumbo frames?
 * If there's no data, return zero.
 * On error, return negative.
 */
int tap_read(char* buf, size_t count)
{
    return read(tap_fd, buf, count);
}

/*
 * (proxy call to POSIX write; man write)
 * Tries to write data to the tap device. n bytes from buf.
 * Returns bytes written if successful, negative if error.
 */
int tap_write(char* buf, size_t count) 
{
    return write(tap_fd, buf, count);
}
