#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>

#include <rte_config.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_launch.h>
#include <rte_cycles.h>

#include <flextcp_plif.h>

#include "internal.h"
#include "fastemu.h"

struct core_load {
  uint64_t cyc_busy;
};

unsigned fp_cores_max;
volatile unsigned fp_cores_cur = 1;
volatile unsigned fp_scale_to = 0;

static unsigned threads_launched = 0;
int exited;

struct core_load *core_loads = NULL;

static int parse_params(int *argc, char ***argv);
static int start_threads(void);
static void thread_error(void);
static int common_thread(void *arg);

#ifdef SPLITTCP
static int glob_argc;
static char **glob_argv;

int kernel_main(int argc, char *argv[]);

static void *kernel_thread(void *arg)
{
  kernel_main(glob_argc, glob_argv);
  return NULL;
}
#endif

int main(int argc, char *argv[])
{
  int res = EXIT_SUCCESS;

  if (dma_preinit() != 0) {
    res = EXIT_FAILURE;
    goto error_exit;
  }

  if (parse_params(&argc, &argv) != 0) {
    res = EXIT_FAILURE;
    fprintf(stderr, "parse params failed\n");
    goto error_exit;
  }

  if ((core_loads = calloc(fp_cores_max, sizeof(*core_loads))) == NULL) {
    res = EXIT_FAILURE;
    fprintf(stderr, "core loads alloc failed\n");
    goto error_exit;
  }

  if (dma_init(fp_cores_max) != 0) {
    res = EXIT_FAILURE;
    fprintf(stderr, "dma init failed\n");
    goto error_exit;
  }

  if (network_init(fp_cores_max, fp_cores_max) != 0) {
    res = EXIT_FAILURE;
    fprintf(stderr, "network init failed\n");
    goto error_dma_cleanup;
  }

  if (qman_init(fp_cores_max) != 0) {
    res = EXIT_FAILURE;
    fprintf(stderr, "qman init failed\n");
    goto error_network_cleanup;
  }

  if (dataplane_init(fp_cores_max) != 0) {
    res = EXIT_FAILURE;
    fprintf(stderr, "dpinit failed\n");
    goto error_qman_cleanup;
  }

  dma_set_ready();

  printf("flexnic ready\n");
  fflush(stdout);

  if (start_threads() != 0) {
    res = EXIT_FAILURE;
    goto error_dataplane_cleanup;
  }

#ifdef SPLITTCP
  glob_argc = argc;
  glob_argv = argv;

  // Start kernel thread
  pthread_t kernel;
  int r = pthread_create(&kernel, NULL, kernel_thread, NULL);
  assert(r == 0);
#endif

#ifndef DATAPLANE_STATS
  if (threads_launched < fp_cores_max) {
    return common_thread((void *) (uintptr_t) threads_launched);
  } else {
    pause();
  }
#else
  while (1) {
    sleep(1);
    dataplane_dump_stats();
    dma_dump_stats();
  }
#endif

error_dataplane_cleanup:
  /* TODO */
error_qman_cleanup:
  /* TODO */
error_network_cleanup:
  network_cleanup();
error_dma_cleanup:
  dma_cleanup();
error_exit:
  return res;
}

static int parse_params(int *argc, char ***argv)
{
  int n;
  unsigned long x;
  char *end;

  /* initialize dpdk */
  if ((n = rte_eal_init(*argc, *argv)) < 0) {
    goto error_exit;
  }

  *argc -= n;
  *argv += n;

  if (*argc >= 2) {
    /* one argument -> common threads */
    x = strtoul((*argv)[1], &end, 0);
    if (*end) {
      goto error_exit;
    }

    fp_cores_max = x;
    /*fp_cores_cur = x;*/

    (*argc)--;
    (*argv)[1] = (*argv)[0];
    (*argv)++;
  } else {
    goto error_exit;
  }
  return 0;

error_exit:
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "    %s MAX_CORES\n", (*argv)[0]);
  return -1;
}

static int common_thread(void *arg)
{
  uint16_t id = (uintptr_t) arg;
  struct dataplane_context *ctx;
  struct qman_thread *qman_t;
  struct network_rx_thread *rx_t;
  struct network_tx_thread *tx_t;

  /* initialize trace if enabled */
#ifdef FLEXNIC_TRACING
  if (trace_thread_init(id) != 0) {
    fprintf(stderr, "initializing trace failed\n");
    goto error_trace;
  }
#endif

  /* initialize rx, tx, and qman handles */
  if ((qman_t = qman_thread_init(id)) == NULL) {
    fprintf(stderr, "initializing qman thread failed\n");
    goto error_qman;
  }
  if ((rx_t = network_rx_thread_init(id)) == NULL) {
    fprintf(stderr, "initializing rx thread failed\n");
    goto error_rx;
  }
  if ((tx_t = network_tx_thread_init(id)) == NULL) {
    fprintf(stderr, "initializing tx thread failed\n");
    goto error_tx;
  }

  /* initialize data plane context */
  if ((ctx = dataplane_context_init(id, rx_t, tx_t, qman_t)) == NULL) {
    fprintf(stderr, "initializing data plane context\n");
    goto error_dpctx;
  }

  /* poll doorbells and network */
  dataplane_loop(ctx);

  dataplane_context_destroy(ctx);
  return 0;

error_dpctx:
  dataplane_context_destroy(ctx);
error_tx:
  /* TODO */
error_rx:
  /* TODO */
error_qman:
  /* TODO */
#ifdef FLEXNIC_TRACING
error_trace:
#endif
  thread_error();
  return -1;
}

static int start_threads(void)
{
  unsigned cores_avail, cores_needed, core;
  void *arg;

#ifdef DATAPLANE_STATS
  cores_avail = rte_lcore_count() - 1;
#else
  cores_avail = rte_lcore_count();
#endif
  cores_needed = fp_cores_max;

  /* check that we have enough cores */
  if (cores_avail < cores_needed) {
    fprintf(stderr, "Not enough cores: got %u need %u\n", cores_avail,
        cores_needed);
    return -1;
  }

  /* start common threads */
  RTE_LCORE_FOREACH_SLAVE(core) {
    if (threads_launched < fp_cores_max) {
      arg = (void *) (uintptr_t) threads_launched;
      if (rte_eal_remote_launch(common_thread, arg, core) != 0) {
	fprintf(stderr, "ERROR\n");
        return -1;
      }
      threads_launched++;
    }
  }

  return 0;
}

static void thread_error(void)
{
  fprintf(stderr, "thread_error\n");
  abort();
}

int flexnic_scale_to(uint32_t cores)
{
  if (fp_scale_to != 0) {
    fprintf(stderr, "flexnic_scale_to: already scaling\n");
    return -1;
  }

  fp_scale_to = cores;

  util_flexnic_kick(&pl_memory->kctx[0]);
  return 0;
}

void flexnic_loadmon(uint32_t ts)
{
  uint64_t cyc_busy = 0, x, tsc, cycles, id_cyc;
  unsigned i, num_cores;
  static uint64_t ewma_busy = 0, ewma_cycles = 0, last_tsc = 0;
  static int waiting = 1, waiting_n = 0, count = 0;

  num_cores = fp_cores_cur;

  /* sum up busy cycles from all cores */
  for (i = 0; i < num_cores; i++) {
    if (ctxs[i] == NULL)
      return;

    x = ctxs[i]->loadmon_cyc_busy;
    cyc_busy += x - core_loads[i].cyc_busy;
    core_loads[i].cyc_busy = x;
  }

  /* measure cpu cycles since last call */
  tsc = rte_get_tsc_cycles();
  if (last_tsc == 0) {
    last_tsc = tsc;
    return;
  }
  cycles = tsc - last_tsc;
  last_tsc = tsc;

  /* ewma for busy cycles and total cycles */
  ewma_busy = (7 * ewma_busy + cyc_busy) / 8;
  ewma_cycles = (7 * ewma_cycles + cycles) / 8;

  /* periodically print out staticstics */
  if (count++ % 100 == 0)
    fprintf(stderr, "flexnic_loadmon: status cores = %u   busy = %lu  "
        "cycles =%lu\n", num_cores, ewma_busy, ewma_cycles);

  /* waiting period after scaling decsions */
  if (waiting && ++waiting_n < 10)
    return;

  /* calculate idle cycles */
  id_cyc = num_cores * ewma_cycles - ewma_busy;

  /* scale down if idle iterations more than 1.25 cores are idle */
  if (num_cores > 1 && id_cyc > ewma_cycles * 5 / 4) {
    fprintf(stderr, "flexnic_loadmon: down cores = %u   idle_cyc = %lu  "
        "1.2 cores = %lu\n", num_cores, id_cyc, ewma_cycles * 5 / 4);
    flexnic_scale_to(num_cores - 1);
    waiting = 1;
    waiting_n = 0;
    return;
  }

  /* scale up if idle iterations less than .2 of a core */
  if (num_cores < fp_cores_max && id_cyc < ewma_cycles / 5) {
    fprintf(stderr, "flexnic_loadmon: up cores = %u   idle_cyc = %lu  "
        "0.2 cores = %lu\n", num_cores, id_cyc,  ewma_cycles / 5);
    flexnic_scale_to(num_cores + 1);
    waiting = 1;
    waiting_n = 0;
    return;
  }
}
