#ifndef APPIF_H_
#define APPIF_H_

#include <stdbool.h>
#include <stdint.h>

#include "internal.h"
#include <kernel_appif.h>

struct app_doorbell {
  uint32_t id;
  /* only for freelist */
  struct app_doorbell *next;
};

struct app_context {
  struct application *app;
  struct packetmem_handle *kin_handle;
  void *kin_base;
  uint32_t kin_len;
  uint32_t kin_pos;

  struct packetmem_handle *kout_handle;
  void *kout_base;
  uint32_t kout_len;
  uint32_t kout_pos;

  struct app_doorbell *doorbell;

  int ready, evfd;
  uint32_t last_ts;
  struct app_context *next;

  struct {
    struct packetmem_handle *rxq;
    struct packetmem_handle *txq;
  } handles[];
};

struct application {
  int fd;
  struct nbqueue_el nqe;
  size_t req_rx;
  struct kernel_uxsock_request req;
  size_t resp_sz;
  struct kernel_uxsock_response *resp;

  struct app_context *contexts;
  struct application *next;
  struct app_context *need_reg_ctx;
  struct app_context *need_reg_ctx_done;

  struct connection *conns;
  struct listener   *listeners;

  struct nicif_completion comp;

  uint16_t id;
  volatile bool closed;
};

/**
 * Poll kernel->app context queue.
 *
 * @param app Application to poll
 * @param ctx Context to poll
 */
unsigned appif_ctx_poll(struct application *app, struct app_context *ctx);

#endif /* ndef APPIF_H_ */
