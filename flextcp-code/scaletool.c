#define _GNU_SOURCE
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <flextcp.h>
#include <utils.h>

int flextcp_kernel_reqscale(struct flextcp_context *ctx, uint32_t cores);

int main(int argc, char *argv[])
{
    unsigned cores;
    struct flextcp_context ctx;

    if (argc != 2) {
        fprintf(stderr, "Usage: ./scaletool CORES\n");
        return EXIT_FAILURE;
    }

    cores = atoi(argv[1]);

    if (flextcp_init() != 0) {
        fprintf(stderr, "flextcp_init failed\n");
        return EXIT_FAILURE;
    }

    if (flextcp_context_create(&ctx) != 0) {
        fprintf(stderr, "flextcp_context_create failed\n");
        return EXIT_FAILURE;
    }

    if (flextcp_kernel_reqscale(&ctx, cores) != 0) {
        fprintf(stderr, "flextcp_kernel_reqscale failed\n");
        return EXIT_FAILURE;
    }

    sleep(1);

    return EXIT_SUCCESS;
}
