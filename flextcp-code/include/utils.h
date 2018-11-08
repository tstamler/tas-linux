#ifndef UTILS_H_
#define UTILS_H_

#include <stdint.h>
#include <arpa/inet.h>

#define MIN(a,b) ((b) < (a) ? (b) : (a))
#define MAX(a,b) ((b) > (a) ? (b) : (a))
#define MEM_BARRIER() __asm__ volatile("" ::: "memory")
#define STATIC_ASSERT(COND,MSG) typedef char static_assertion_##MSG[(COND)?1:-1]
#define LIKELY(x) __builtin_expect((x),1)
#define UNLIKELY(x) __builtin_expect((x),0)

#define POLL_CYCLE	10000		// 10ms

int util_parse_ipv4(const char *s, uint32_t *ip);
int util_parse_mac(const char *s, uint64_t *mac);

void util_dump_mem(const void *b, size_t len);

/* types for big endian integers to catch those errors with types */
struct beui16 { uint16_t x; } __attribute__((packed));
struct beui32 { uint32_t x; } __attribute__((packed));
struct beui64 { uint64_t x; } __attribute__((packed));
typedef struct beui16 beui16_t;
typedef struct beui32 beui32_t;
typedef struct beui64 beui64_t;

static inline uint16_t f_beui16(beui16_t x) { return __builtin_bswap16(x.x); }
static inline uint32_t f_beui32(beui32_t x) { return __builtin_bswap32(x.x); }
static inline uint64_t f_beui64(beui64_t x) { return __builtin_bswap64(x.x); }

static inline beui16_t t_beui16(uint16_t x)
{
  beui16_t b;
  b.x = __builtin_bswap16(x);
  return b;
}

static inline beui32_t t_beui32(uint32_t x)
{
  beui32_t b;
  b.x = __builtin_bswap32(x);
  return b;
}

static inline beui64_t t_beui64(uint64_t x)
{
  beui64_t b;
  b.x = __builtin_bswap64(x);
  return b;
}

static inline uint64_t util_rdtsc(void)
{
    uint32_t eax, edx;
    asm volatile ("rdtsc" : "=a" (eax), "=d" (edx));
    return ((uint64_t) edx << 32) | eax;
}

#endif /* ndef UTILS_H_ */
