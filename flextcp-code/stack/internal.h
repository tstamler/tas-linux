#ifndef INTERNAL_H_
#define INTERNAL_H_

#include <flextcp.h>
#include <flextcp_plif.h>
#include <utils_circ.h>

#define OPAQUE_OBJ 1ULL
#define OPAQUE_ISOBJ(x) (!!((x) & OPAQUE_OBJ))
#define OPAQUE_PTR(x) ((void *) (uintptr_t) ((x) & ~OPAQUE_OBJ))
#define OPAQUE_FROMOBJ(x) ((uintptr_t) (x) | OPAQUE_OBJ)
#define OPAQUE_FROMCONN(x) ((uintptr_t) (x))
#define OPAQUE(x,o) ((uintptr_t) (x) | ((o) ? OPAQUE_OBJ : 0))

#define OBJ_FLAG_DONE 1
#define OBJ_FLAG_PREV_FREED 2

enum conn_state {
  CONN_CLOSED,
  CONN_OPEN_REQUESTED,
  CONN_ACCEPT_REQUESTED,
  CONN_OPEN,
};

extern void *flexnic_mem;
extern int flexnic_evfd[FLEXTCP_MAX_FTCPCORES];

int flextcp_kernel_connect(void);
int flextcp_kernel_newctx(struct flextcp_context *ctx);
void flextcp_kernel_kick(void);

int flextcp_context_tx_alloc(struct flextcp_context *ctx,
    struct flextcp_pl_atx **atx, uint16_t core);
void flextcp_context_tx_done(struct flextcp_context *ctx, uint16_t core);

uint32_t flextcp_conn_txbuf_available(struct flextcp_connection *conn);

static inline void oconn_lock(struct flextcp_obj_connection *oc)
{
  while (__sync_lock_test_and_set(&oc->lock, 1) == 1) {
    __builtin_ia32_pause();
  }
}

static inline void oconn_unlock(struct flextcp_obj_connection *oc)
{
  __sync_lock_release(&oc->lock);
}

#endif /* ndef INTERNAL_H_ */
