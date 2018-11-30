#include <stddef.h>

int tap_init();
int tap_poll();
int tap_read(uint8_t*, size_t);
int tap_write(uint8_t*, size_t);
