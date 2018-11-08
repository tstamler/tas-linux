#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>

#include <utils.h>
#include <rte_config.h>
#include <rte_malloc.h>
#include <rte_memcpy.h>

#include "internal.h"

#define DMA_DEBUG(x...) do {} while (0)
//#define DMA_DEBUG(x...) fprintf(stderr, x)

struct dma_thread {
};

static struct flexnic_info *info;
struct flextcp_pl_mem *pl_memory = NULL;
static void *dma_mem;
static struct dma_thread **threads;
static unsigned num_threads;
static volatile unsigned thread_id;

/* create shared memory region */
static void *util_create_shmsiszed(const char *name, size_t size, void *addr);
/* destroy shared memory region */
static void destroy_shm(const char *name, size_t size, void *addr);
/* create shared memory region using huge pages */
static void *util_create_shmsiszed_huge(const char *name, size_t size,
    void *addr) __attribute__((used));
/* destroy shared huge page memory region */
static void destroy_shm_huge(const char *name, size_t size, void *addr)
    __attribute__((used));

/* Allocate DMA memory before DPDK grabs all huge pages */
int dma_preinit(void)
{
  /* create shm for dma memory */
#ifdef FLEXNIC_USE_HUGEPAGES
  dma_mem = util_create_shmsiszed_huge(FLEXNIC_NAME_DMA_MEM,
      FLEXNIC_DMA_MEM_SIZE, NULL);
#else
  dma_mem = util_create_shmsiszed(FLEXNIC_NAME_DMA_MEM, FLEXNIC_DMA_MEM_SIZE,
      NULL);
#endif
  if (dma_mem == NULL) {
    fprintf(stderr, "mapping flexnic dma memory failed\n");
    return -1;
  }

  /* create shm for internal memory */
#ifdef FLEXNIC_USE_HUGEPAGES
  pl_memory = util_create_shmsiszed_huge(FLEXNIC_NAME_INTERNAL_MEM,
      FLEXNIC_INTERNAL_MEM_SIZE, NULL);
#else
  pl_memory = util_create_shmsiszed(FLEXNIC_NAME_INTERNAL_MEM,
      FLEXNIC_INTERNAL_MEM_SIZE, NULL);
#endif
  if (pl_memory == NULL) {
    fprintf(stderr, "mapping flexnic internal memory failed\n");
    dma_cleanup();
    return -1;
  }

  return 0;
}

int dma_init(unsigned num)
{
  /* allocate array for thread handle pointers */
  num_threads = num;
  if ((threads = calloc(num, sizeof(*threads))) == NULL) {
      goto error_out;
  }

  umask(0);

  /* create shm for info */
  info = util_create_shmsiszed(FLEXNIC_NAME_INFO, FLEXNIC_INFO_BYTES, NULL);
  if (info == NULL) {
    fprintf(stderr, "mapping flexnic info failed\n");
    goto error_freethreads;
  }

  info->dma_mem_size = FLEXNIC_DMA_MEM_SIZE;
  info->internal_mem_size = FLEXNIC_INTERNAL_MEM_SIZE;
  info->db_num = FLEXNIC_NUM_DOORBELL;
  info->db_qlen = FLEXNIC_DB_QLEN;
  info->qmq_num = FLEXNIC_NUM_QMQUEUES;
  info->cores_num = num;

  return 0;

error_freethreads:
  dma_cleanup();
  free(threads);
error_out:
  return -1;
}

void dma_cleanup(void)
{
  /* cleanup internal memory region */
  if (pl_memory != NULL) {
#ifdef FLEXNIC_USE_HUGEPAGES
    destroy_shm_huge(FLEXNIC_NAME_INTERNAL_MEM, FLEXNIC_INTERNAL_MEM_SIZE,
        pl_memory);
#else
    destroy_shm(FLEXNIC_NAME_INTERNAL_MEM, FLEXNIC_INTERNAL_MEM_SIZE,
        pl_memory);
#endif
  }

  /* cleanup dma memory region */
  if (dma_mem != NULL) {
#ifdef FLEXNIC_USE_HUGEPAGES
    destroy_shm_huge(FLEXNIC_NAME_DMA_MEM, FLEXNIC_DMA_MEM_SIZE, dma_mem);
#else
    destroy_shm(FLEXNIC_NAME_DMA_MEM, FLEXNIC_DMA_MEM_SIZE, dma_mem);
#endif
  }

  /* cleanup info memory region */
  if (info != NULL) {
    destroy_shm(FLEXNIC_NAME_INFO, FLEXNIC_INFO_BYTES, info);
  }

  free(threads);
  threads = NULL;
}

void dma_set_ready(void)
{
  info->mac_addr = 0;
  memcpy(&info->mac_addr, &eth_addr, sizeof(eth_addr));
  info->flags |= FLEXNIC_FLAG_READY;
}

struct dma_thread *dma_thread_init(void)
{
  struct dma_thread *t;
  unsigned tid;

  /* allocate thread struct */
  if ((t = rte_zmalloc("dma thread struct", sizeof(*t), 0)) == NULL) {
    return NULL;
  }
  /* get id */
  tid = __sync_add_and_fetch(&thread_id, 1);
  assert(tid <= num_threads);

  threads[tid - 1] = t;
  return t;
}

#ifdef DATAPLANE_STATS
void dma_dump_stats(void)
{
}
#endif

int dma_read(uintptr_t addr, size_t len, void *buf)
{
  DMA_DEBUG("dma_read(%llx, %llx)\n", (long long) addr, (long long) len);

  /* validate address */
  if (addr + len < addr || addr + len > FLEXNIC_DMA_MEM_SIZE) {
    return -1;
  }

  rte_memcpy(buf, (uint8_t *) dma_mem + addr, len);

#ifdef FLEXNIC_TRACE_DMA
  struct flexnic_trace_entry_dma evt = {
      .addr = addr,
      .len = len,
    };
  trace_event2(FLEXNIC_TRACE_EV_DMARD, sizeof(evt), &evt,
      MIN(len, UINT16_MAX - sizeof(evt)), buf);
#endif

  return 0;
}

int dma_write(uintptr_t addr, size_t len, const void *buf)
{
  DMA_DEBUG("dma_write(%llx, %llx)\n", (long long) addr, (long long) len);

  /* validate address */
  if (addr + len < addr || addr + len > FLEXNIC_DMA_MEM_SIZE) {
    return -1;
  }

  rte_memcpy((uint8_t *) dma_mem + addr, buf, len);

#ifdef FLEXNIC_TRACE_DMA
  struct flexnic_trace_entry_dma evt = {
      .addr = addr,
      .len = len,
    };
  trace_event2(FLEXNIC_TRACE_EV_DMAWR, sizeof(evt), &evt,
      MIN(len, UINT16_MAX - sizeof(evt)), buf);
#endif

  return 0;
}

int dma_pointer(uintptr_t addr, size_t len, void **buf)
{
  DMA_DEBUG("dma_write(%llx, %llx)\n", (long long) addr, (long long) len);

  /* validate address */
  if (addr + len < addr || addr + len > FLEXNIC_DMA_MEM_SIZE) {
    return -1;
  }

  *buf = (uint8_t *) dma_mem + addr;

  return 0;

}

static void *util_create_shmsiszed(const char *name, size_t size, void *addr)
{
  int fd;
  void *p;

  if ((fd = shm_open(name, O_CREAT | O_RDWR, 0666)) == -1) {
    perror("shm_open failed");
    goto error_out;
  }
  if (ftruncate(fd, size) != 0) {
    perror("ftruncate failed");
    goto error_remove;
  }

  if ((p = mmap(addr, size, PROT_READ | PROT_WRITE,
      MAP_SHARED | (addr == NULL ? 0 : MAP_FIXED) | MAP_POPULATE, fd, 0)) ==
      (void *) -1)
  {
    perror("mmap failed");
    goto error_remove;
  }

  memset(p, 0, size);

  close(fd);
  return p;

error_remove:
  close(fd);
  shm_unlink(name);
error_out:
  return NULL;
}

static void destroy_shm(const char *name, size_t size, void *addr)
{
  if (munmap(addr, size) != 0) {
    fprintf(stderr, "Warning: munmap failed (%s)\n", strerror(errno));
  }
  shm_unlink(name);
}

static void *util_create_shmsiszed_huge(const char *name, size_t size,
    void *addr)
{
  int fd;
  void *p;
  char path[128];

  snprintf(path, sizeof(path), "%s/%s", FLEXNIC_HUGE_PREFIX, name);

  if ((fd = open(path, O_CREAT | O_RDWR, 0666)) == -1) {
    perror("util_create_shmsiszed: open failed");
    goto error_out;
  }
  if (ftruncate(fd, size) != 0) {
    perror("util_create_shmsiszed: ftruncate failed");
    goto error_remove;
  }

  if ((p = mmap(addr, size, PROT_READ | PROT_WRITE,
      MAP_SHARED | (addr == NULL ? 0 : MAP_FIXED) | MAP_POPULATE, fd, 0)) ==
      (void *) -1)
  {
    perror("util_create_shmsiszed: mmap failed");
    goto error_remove;
  }

  memset(p, 0, size);

  close(fd);
  return p;

error_remove:
  close(fd);
  shm_unlink(name);
error_out:
  return NULL;
}

static void destroy_shm_huge(const char *name, size_t size, void *addr)
{
  char path[128];

  snprintf(path, sizeof(path), "%s/%s", FLEXNIC_HUGE_PREFIX, name);

  if (munmap(addr, size) != 0) {
    fprintf(stderr, "Warning: munmap failed (%s)\n", strerror(errno));
  }
  unlink(path);
}
