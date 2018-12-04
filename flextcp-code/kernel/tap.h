#include <stddef.h>

int tap_init();
void* tap_poll(void*);
int tap_read(uint8_t*, size_t);
int tap_write(uint8_t*, size_t);
