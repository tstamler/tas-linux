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

#include <flexnic.h>

static void *map_region(const char *name, size_t len);
static void *map_region_huge(const char *name, size_t len)
  __attribute__((used));

static struct flexnic_info *info = NULL;

int flexnic_driver_connect(struct flexnic_info **p_info, void **p_mem_start)
{
  void *m;
  volatile struct flexnic_info *fi;
  int err_ret = -1;

  /* return error, if already connected */
  if (info != NULL) {
    fprintf(stderr, "flexnic_driver_connect: already connected\n");
    goto error_exit;
  }

  /* open and map flexnic info shm region */
  if ((m = map_region(FLEXNIC_NAME_INFO, FLEXNIC_INFO_BYTES)) == NULL) {
    perror("flexnic_driver_connect: map_region info failed");
    goto error_exit;
  }

  /* abort if not ready yet */
  fi = (volatile struct flexnic_info *) m;
  if ((fi->flags & FLEXNIC_FLAG_READY) != FLEXNIC_FLAG_READY) {
    err_ret = 1;
    goto error_unmap_info;
  }

  /* open and map flexnic info shm region */
#ifdef FLEXNIC_USE_HUGEPAGES
  if ((m = map_region_huge(FLEXNIC_NAME_DMA_MEM, fi->dma_mem_size)) == NULL)
#else
  if ((m = map_region(FLEXNIC_NAME_DMA_MEM, fi->dma_mem_size)) == NULL)
#endif
  {
    perror("flexnic_driver_connect: mapping dma memory failed");
    goto error_unmap_info;
  }

  *p_info = info = (struct flexnic_info *) fi;
  *p_mem_start = m;
  return 0;

error_unmap_info:
  munmap(m, FLEXNIC_INFO_BYTES);
error_exit:
  return err_ret;
}

int flexnic_driver_internal(void **int_mem_start)
{
  void *m;

  if (info == NULL) {
    fprintf(stderr, "flexnic_driver_internal: driver not connected\n");
    return -1;
  }

  /* open and map flexnic internal memory shm region */
#ifdef FLEXNIC_USE_HUGEPAGES
  if ((m = map_region_huge(FLEXNIC_NAME_INTERNAL_MEM, info->internal_mem_size))
      == NULL)
#else
  if ((m = map_region(FLEXNIC_NAME_INTERNAL_MEM, info->internal_mem_size))
      == NULL)
#endif
  {
    perror("flexnic_driver_internal: map_region failed");
    return -1;
  }

  *int_mem_start = m;
  return 0;
}

static void *map_region(const char *name, size_t len)
{
  int fd;
  void *m;

  if ((fd = shm_open(name, O_RDWR, 0)) == -1) {
    perror("map_region: shm_open memory failed");
    return NULL;
  }
  m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
  close(fd);
  if (m == (void *) -1) {
    perror("flexnic_driver_connect: mmap failed");
    return NULL;
  }

  return m;
}

static void *map_region_huge(const char *name, size_t len)
{
  int fd;
  void *m;
  char path[128];

  snprintf(path, sizeof(path), "%s/%s", FLEXNIC_HUGE_PREFIX, name);

  if ((fd = open(path, O_RDWR)) == -1) {
    perror("map_region: shm_open memory failed");
    return NULL;
  }
  m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
  close(fd);
  if (m == (void *) -1) {
    perror("flexnic_driver_connect: mmap failed");
    return NULL;
  }

  return m;
}
