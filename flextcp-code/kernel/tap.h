#include <stddef.h>

int tapif_init();
int tapif_poll();
int tap_read(uint8_t*, size_t);
int tap_write(uint8_t*, size_t);
