#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <utils_timeout.h>
#include <utils.h>
#include <flextcp_plif.h>
#include <assert.h>
#include <unistd.h>

int util_parse_ipv4(const char *s, uint32_t *ip)
{
  if (inet_pton(AF_INET, s, ip) != 1) {
    return -1;
  }
  *ip = htonl(*ip);
  return 0;
}

int util_parse_mac(const char *s, uint64_t *mac)
{
  char buf[18];
  int i;
  uint64_t x, y;

  /* mac address has length 17 = 2x6 digits + 5 colons */
  if (strlen(s) != 17 || s[2] != ':' || s[5] != ':' ||
      s[8] != ':' || s[11] != ':' || s[14] != ':')
  {
    return -1;
  }
  memcpy(buf, s, sizeof(buf));

  /* replace colons by NUL bytes to separate strings */
  buf[2] = buf[5] = buf[8] = buf[11] = buf[14] = 0;

  y = 0;
  for (i = 5; i >= 0; i--) {
      if (!isxdigit(buf[3 * i]) || !isxdigit(buf[3 * i + 1])) {
          return -1;
      }
      x = strtoul(&buf[3 * i], NULL, 16);
      y = (y << 8) | x;
  }
  *mac = y;

  return 0;
}

void util_dump_mem(const void *mem, size_t len)
{
  const uint8_t *b = mem;
  size_t i;
  for (i = 0; i < len; i++) {
    fprintf(stderr, "%02x ", b[i]);
  }
  fprintf(stderr, "\n");
}

void util_flexnic_kick(struct flextcp_pl_appctx *ctx)
{
  uint32_t now = util_timeout_time_us();

  /* fprintf(stderr, "kicking app/flexnic?\n"); */

  if(now - ctx->last_ts > POLL_CYCLE) {
    // Kick kernel
    //fprintf(stderr, "kicking app/flexnic on %d in %p, &evfd: %p\n", ctx->evfd, ctx, &(ctx->evfd)); 
    uint64_t val = 1;
    int r = write(ctx->evfd, &val, sizeof(uint64_t));
    
	assert(r == sizeof(uint64_t));
  }

  ctx->last_ts = now;
}
