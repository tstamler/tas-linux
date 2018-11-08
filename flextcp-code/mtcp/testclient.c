#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <netdb.h>
#include <errno.h>
#include <assert.h>
#include <locale.h>
#include <inttypes.h>

#include "socket_shim.h"

#ifdef USE_MTCP
# include <mtcp_api.h>
# include <mtcp_epoll.h>
#else
# include <sys/epoll.h>
#endif

#define MIN(a,b) ((b) < (a) ? (b) : (a))

#define CONN_DEBUG(c, co, x...) do { } while (0)
/*#define CONN_DEBUG(c, co, x...) \
    do { printf("%d.%d: ", (int) c->id, co->fd); \
         printf(x); } while (0)*/

//#define PRINT_STATS
//#define PRINT_TSS
#ifdef PRINT_STATS
#   define STATS_ADD(c, f, n) c->f += n
#else
#   define STATS_ADD(c, f, n) do { } while (0)
#endif

#define HIST_START_US 0
#define HIST_BUCKET_US 1
#define HIST_BUCKETS (256 * 1024)


enum conn_state {
    CONN_CLOSED = 0,
    CONN_CONNECTING = 1,
    CONN_OPEN = 2,
};

static uint32_t max_pending = 64;
static uint32_t message_size = 64;
static uint32_t num_conns = 8;
static struct sockaddr_in addr;

struct connection {
    enum conn_state state;
    int fd;
    int ep_wr;
    uint32_t pending;
    uint32_t tx_remain;
    uint32_t rx_remain;
    void *rx_buf;
    void *tx_buf;
#ifdef PRINT_STATS
    uint64_t cnt;
#endif
};

struct core {
    struct connection *conns;
    uint64_t messages;
#ifdef PRINT_STATS
    uint64_t rx_calls;
    uint64_t rx_short;
    uint64_t rx_fail;
    uint64_t rx_bytes;
    uint64_t tx_calls;
    uint64_t tx_short;
    uint64_t tx_fail;
    uint64_t tx_bytes;
    uint64_t rx_cycles;
    uint64_t tx_cycles;
#endif
    uint32_t *hist;
    int ep;
    ssctx_t sc;
#ifdef USE_MTCP
    mctx_t mc;
#endif
    uint64_t id;
    pthread_t pthread;
} __attribute__((aligned(64)));

static inline uint64_t get_nanos(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
}

static inline uint64_t get_nanos_stats(void)
{
#ifdef PRINT_TSS
    return get_nanos();
#else
    return 0;
#endif
}

static inline void record_latency(struct core *c, uint64_t nanos)
{
    size_t bucket = ((nanos / 1000) - HIST_START_US) / HIST_BUCKET_US;
    if (bucket >= HIST_BUCKETS) {
        bucket = HIST_BUCKETS - 1;
    }
    __sync_fetch_and_add(&c->hist[bucket], 1);
}

#ifdef PRINT_STATS
static inline uint64_t read_cnt(uint64_t *p)
{
  uint64_t v = *p;
  __sync_fetch_and_sub(p, v);
  return v;
}
#endif

static inline void conn_connect(struct core *c, struct connection *co)
{
    int fd, cn, ret;
    ssctx_t sc;
    ss_epev_t ev;

    cn = c->id;
    sc = c->sc;
    CONN_DEBUG(c, co, "Opening new connection\n");

    /* create socket */
    if ((fd = ss_socket(sc, AF_INET, SOCK_STREAM, IPPROTO_TCP))
        < 0)
    {
        perror("creating socket failed");
        fprintf(stderr, "[%d] socket failed\n", cn);
        abort();
    }

    /* make socket non-blocking */
    if ((ret = ss_set_nonblock(sc, fd)) != 0) {
        fprintf(stderr, "[%d] set_nonblock failed: %d\n", cn, ret);
        abort();
    }

    /* disable nagling */
    if (ss_set_nonagle(sc, fd) != 0) {
        fprintf(stderr, "[%d] setsockopt TCP_NODELAY failed\n", cn);
        abort();
    }

    /* add to epoll */
    ev.data.ptr = co;
    ev.events = SS_EPOLLIN | SS_EPOLLOUT | SS_EPOLLHUP | SS_EPOLLERR;
    if (ss_epoll_ctl(sc, c->ep, SS_EPOLL_CTL_ADD, fd, &ev) < 0) {
      fprintf(stderr, "[%d] adding to epoll failed\n", cn);
    }

    /* initiate non-blocking connect*/
    ret = ss_connect(sc, fd, (struct sockaddr *) &addr, sizeof(addr));
    if (ret == 0) {
        /* success */
        CONN_DEBUG(c, co, "Connection succeeded\n");
        co->state = CONN_OPEN;
    } else if (ret < 0 && errno == EINPROGRESS) {
        /* still going on */
        CONN_DEBUG(c, co, "Connection pending: %d\n", fd);
        co->state = CONN_CONNECTING;
    } else {
        /* opening connection failed */
        fprintf(stderr, "[%d] connect failed: %d\n", cn, ret);
        abort();
    }

    co->fd = fd;
    co->ep_wr = 1;
    co->pending = 0;
    co->rx_remain = message_size;
    co->tx_remain = message_size;
#ifdef PRINT_STATS
    co->cnt = 0;
#endif
}

static void prepare_core(struct core *c)
{
    int cn = c->id;
    uint32_t i;
    uint8_t *buf;
    ssctx_t sc;
#ifdef USE_MTCP
    int ret;
#endif

    /* Affinitize threads */
#ifdef USE_MTCP
    if ((ret = mtcp_core_affinitize(cn)) != 0) {
        fprintf(stderr, "[%d] mtcp_core_affinitize failed: %d\n", cn, ret);
        abort();
    }

    if ((sc = mtcp_create_context(cn)) == NULL) {
        fprintf(stderr, "[%d] mtcp_create_context failed\n", cn);
        abort();
    }

    if ((ret = mtcp_init_rss(sc, INADDR_ANY, 1, addr.sin_addr.s_addr,
                    addr.sin_port)) != 0)
    {
        fprintf(stderr, "[%d] mtcp_init_rss failed\n", cn);
        abort();
    }
#else
    sc = NULL;
#endif
    c->sc = sc;

    /* create epoll */
    if ((c->ep = ss_epoll_create(sc, 4 * num_conns)) < 0) {
        fprintf(stderr, "[%d] epoll_create failed\n", c->ep);
        abort();
    }

    /* Allocate histogram */
    if ((c->hist = calloc(HIST_BUCKETS, sizeof(*c->hist))) == NULL) {
        fprintf(stderr, "[%d] allocating histogram failed\n", cn);
        abort();
    }

    /* Allocate connection structs */
    if ((c->conns = calloc(num_conns, sizeof(*c->conns))) == NULL) {
        fprintf(stderr, "[%d] allocating connection structs failed\n", cn);
        abort();
    }

    /* Initiate connections */
    for (i = 0; i < num_conns; i++) {
        if ((buf = malloc(message_size * 2)) == NULL) {
            fprintf(stderr, "[%d] allocating conn buffer failed\n", cn);
        }
        c->conns[i].rx_buf = buf;
        c->conns[i].tx_buf = buf + message_size;
        c->conns[i].state = CONN_CLOSED;
        c->conns[i].fd = -1;
        conn_connect(c, &c->conns[i]);
    }
}

static inline void conn_error(struct core *c, struct connection *co,
    const char *msg)
{
    CONN_DEBUG(c, co, "Closing connection on error\n");
    fprintf(stderr, "Closing connection %p on %p (%s)\n", co, c, msg);
    ss_close(c->sc, co->fd);
    co->state = CONN_CLOSED;
    co->fd = -1;
    conn_connect(c, co);
}

static inline void conn_events(struct core *c, struct connection *co,
        uint32_t events)
{
    int fd, ret, status, wait_wr;
    int cn;
    uint64_t *rx_ts, *tx_ts;
    void *rx_buf, *tx_buf;
    socklen_t slen;
    ssctx_t sc;
    ss_epev_t ev;
#ifdef PRINT_STATS
    uint64_t tsc;
#endif

    sc = c->sc;
    cn = c->id;
    fd = co->fd;

    /* check for errors on the connection */
    if ((events & (SS_EPOLLERR | SS_EPOLLHUP)) != 0) {
        conn_error(c, co, "error/hup on epoll");
        return;
    }

    if (co->state == CONN_CONNECTING) {
        CONN_DEBUG(c, co, "Event on connecting connection\n");
        slen = sizeof(status);
        if (ss_getsockopt(sc, fd, SOL_SOCKET, SO_ERROR, &status, &slen) < 0) {
            perror("getsockopt failed");
            fprintf(stderr, "[%d] getsockopt for conn status failed\n", cn);
            abort();
        }

        if (status != 0) {
            conn_error(c, co, "getsockopt for connect failed");
            return;
        }

        CONN_DEBUG(c, co, "Connection successfully opened\n");
        co->state = CONN_OPEN;
    }

    /* receive responses */
    ret = 1;
    rx_buf = co->rx_buf;
    rx_ts = rx_buf;
    while (co->pending > 0 && ret > 0) {
        STATS_ADD(c, rx_calls, 1);
#ifdef PRINT_STATS
        tsc = get_nanos_stats();
#endif
        ret = ss_read(sc, fd, rx_buf + message_size - co->rx_remain,
            co->rx_remain);
        STATS_ADD(c, rx_cycles, get_nanos_stats() - tsc);
        if (ret > 0) {
            STATS_ADD(c, rx_bytes, ret);
            /* received data */
            if (ret < message_size) {
                STATS_ADD(c, rx_short, 1);
            }
            co->rx_remain -= ret;
            if (co->rx_remain == 0) {
                /* received whole message */
                __sync_fetch_and_add(&c->messages, 1);
                record_latency(c, get_nanos() - *rx_ts);
                co->rx_remain = message_size;
                co->pending--;
#ifdef PRINT_STATS
                co->cnt++;
#endif
            }
        } else if (ret < 0 && errno != EAGAIN) {
            /* errror, close connection */
            fprintf(stderr, "[%d] read failed: %d\n", cn, ret);
            conn_error(c, co, "read failed");
            return;
        } else if (ret < 0 && errno == EAGAIN) {
            /* nothing to receive */
            STATS_ADD(c, rx_fail, 1);
        }
    }

    /* send out requests */
    ret = 1;
    wait_wr = 0;
    tx_buf = co->tx_buf;
    tx_ts = tx_buf;
    while ((co->pending < max_pending || max_pending == 0) && ret > 0) {
        /* timestamp if starting a new message */
        if (co->tx_remain == message_size) {
            *tx_ts = get_nanos();
        }

        STATS_ADD(c, tx_calls, 1);
#ifdef PRINT_STATS
        tsc = get_nanos_stats();
#endif
        ret = ss_write(sc, fd, tx_buf + message_size - co->tx_remain,
            co->tx_remain);
        STATS_ADD(c, tx_cycles, get_nanos_stats() - tsc);
        if (ret > 0) {
            STATS_ADD(c, tx_bytes, ret);
            /* sent some data */
            if (ret < message_size) {
                STATS_ADD(c, tx_short, 1);
            }
            co->tx_remain -= ret;
            if (co->tx_remain == 0) {
                /* sent whole message */
                co->pending++;
                co->tx_remain = message_size;
                if (co->pending < max_pending) {
                    /* send next message when epoll tells us to */
                    wait_wr = 1;
                    break;
                }
            }
        } else if (ret < 0 && errno != EAGAIN) {
            /* send failed */
            fprintf(stderr, "[%d] write failed: %d\n", cn, ret);
            conn_error(c, co, "write failed");
            return;
        } else if (ret < 0 && errno == EAGAIN) {
            /* send would block */
            wait_wr = 1;
            STATS_ADD(c, tx_fail, 1);
        }
    }

    /* make sure we epoll for write iff we're actually blocked on writes */
    if (wait_wr != co->ep_wr) {
        ev.data.ptr = co;
        ev.events = SS_EPOLLIN | SS_EPOLLHUP | SS_EPOLLERR |
            (wait_wr ? SS_EPOLLOUT : 0);
        if (ss_epoll_ctl(sc, c->ep, SS_EPOLL_CTL_MOD, fd, &ev) != 0) {
            fprintf(stderr, "[%d] epoll_ctl failed\n", cn);
            abort();
        }
        co->ep_wr = wait_wr;
    }
}

static void *thread_run(void *arg)
{
    struct core *c = arg;
    int i, cn, ret, ep, num_evs;
    struct connection *co;
    ssctx_t sc;
    ss_epev_t *evs;

    prepare_core(c);
    cn = c->id;
    ep = c->ep;
    sc = c->sc;

    num_evs = 4 * num_conns;
    if ((evs = calloc(num_evs, sizeof(*evs))) == NULL) {
        fprintf(stderr, "[%d] malloc failed\n", cn);
    }

    while (1) {
        /* epoll, wait for events */
        if ((ret = ss_epoll_wait(sc, ep, evs, num_evs, -1)) < 0) {
            fprintf(stderr, "[%d] epoll_wait failed\n", cn);
            abort();
        }

        for (i = 0; i < ret; i++) {
            co = evs[i].data.ptr;
            conn_events(c, co, evs[i].events);
        }
    }
}

static inline void hist_fract_buckets(uint32_t *hist, uint64_t total,
        double *fracs, size_t *idxs, size_t num)
{
    size_t i, j;
    uint64_t sum = 0, goals[num];
    for (j = 0; j < num; j++) {
        goals[j] = total * fracs[j];
    }
    for (i = 0, j = 0; i < HIST_BUCKETS && j < num; i++) {
        sum += hist[i];
        for (; j < num && sum >= goals[j]; j++) {
            idxs[j] = i;
        }
    }
}

static inline int hist_value(size_t i)
{
    if (i == HIST_BUCKETS - 1) {
        return -1;
    }

    return i * HIST_BUCKET_US + HIST_START_US;
}

int main(int argc, char *argv[])
{
#ifdef USE_MTCP
    int ret;
#endif
    int num_threads, i, j;
    struct core *cs;
    uint64_t t_prev, t_cur;
    long double *ttp, tp, tp_total;
    uint32_t *hist, hx;
    uint64_t msg_total;
    double fracs[6] = { 0.5, 0.9, 0.95, 0.99, 0.999, 0.9999 };
    size_t fracs_pos[sizeof(fracs) / sizeof(fracs[0])];

    setlocale(LC_NUMERIC, "");

    if (argc < 5 || argc > 8) {
        fprintf(stderr, "Usage: ./testclient IP PORT CORES CONFIG "
            "[MESSAGE-SIZE] [MAX-PENDING]\n");
        return EXIT_FAILURE;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(argv[1]);
    addr.sin_port = htons(atoi(argv[2]));

    num_threads = atoi(argv[3]);

#ifdef USE_MTCP
    if ((ret = mtcp_init(argv[4])) != 0) {
        fprintf(stderr, "mtcp_init failed: %d\n", ret);
        return EXIT_FAILURE;
    }
#endif

    if (argc >= 6) {
        message_size = atoi(argv[5]);
    }

    if (argc >= 7) {
        max_pending = atoi(argv[6]);
    }

    if (argc >= 8) {
        num_conns = atoi(argv[7]);
    }

    assert(sizeof(*cs) % 64 == 0);
    cs = calloc(num_threads, sizeof(*cs));
    ttp = calloc(num_threads, sizeof(*ttp));
    if (cs == NULL || ttp == NULL) {
        fprintf(stderr, "allocating thread handles failed\n");
        return EXIT_FAILURE;
    }

    if ((hist = calloc(HIST_BUCKETS, sizeof(*hist))) == NULL) {
        fprintf(stderr, "allocating total histogram failed\n");
        abort();
    }


    for (i = 0; i < num_threads; i++) {
        cs[i].id = i;
        if (pthread_create(&cs[i].pthread, NULL, thread_run, cs + i)) {
            fprintf(stderr, "pthread_create failed\n");
            return EXIT_FAILURE;
        }
    }

    t_prev = get_nanos();
    while (1) {
        sleep(1);
        t_cur = get_nanos();
        tp_total = 0;
        msg_total = 0;
        for (i = 0; i < num_threads; i++) {
            tp = cs[i].messages;
            cs[i].messages = 0;
            tp /= (double) (t_cur - t_prev) / 1000000000.;
            ttp[i] = tp;
            tp_total += tp;

            for (j = 0; j < HIST_BUCKETS; j++) {
                hx = cs[i].hist[j];
                msg_total += hx;
                hist[j] += hx;
            }
        }

        hist_fract_buckets(hist, msg_total, fracs, fracs_pos,
                sizeof(fracs) / sizeof(fracs[0]));


        printf("TP: total=%'.2Lf mbps  50p=%d us  90p=%d us  95p=%d us  "
                "99p=%d us  99.9p=%d us  99.99p=%d us  ",
                tp_total * message_size * 8 / 1000000.,
                hist_value(fracs_pos[0]), hist_value(fracs_pos[1]),
                hist_value(fracs_pos[2]), hist_value(fracs_pos[3]),
                hist_value(fracs_pos[4]), hist_value(fracs_pos[5]));

#ifdef PRINT_PERCORE
        for (i = 0; i < num_threads; i++) {
            printf("core[%d]=%'.2Lf mbps  ", i,
                    ttp[i] * message_size * 8 / 1000000.);
        }
#endif
        printf("\n");
#ifdef PRINT_STATS
        printf("stats:\n");
        for (i = 0; i < num_threads; i++) {
            uint64_t rx_calls = read_cnt(&cs[i].rx_calls);
            uint64_t rx_cycles = read_cnt(&cs[i].rx_cycles);
            uint64_t tx_calls = read_cnt(&cs[i].tx_calls);
            uint64_t tx_cycles = read_cnt(&cs[i].tx_cycles);
            uint64_t rxc = (rx_calls == 0 ? 0 : rx_cycles / rx_calls);
            uint64_t txc = (tx_calls == 0 ? 0 : tx_cycles / tx_calls);

            printf("    core %2d: (rt=%"PRIu64", rs=%"PRIu64", rf=%"PRIu64
                ", rxc=%"PRIu64", rb=%"PRIu64", tt=%"PRIu64", ts=%"PRIu64", tf=%"PRIu64
                ", txc=%"PRIu64", tb=%"PRIu64")\n", i,
                rx_calls, read_cnt(&cs[i].rx_short), read_cnt(&cs[i].rx_fail),
                rxc, read_cnt(&cs[i].rx_bytes),
                tx_calls, read_cnt(&cs[i].tx_short), read_cnt(&cs[i].tx_fail),
                txc, read_cnt(&cs[i].tx_bytes));
        }
        for (i = 0; i < num_threads; i++) {
            for (j = 0; j < num_conns; j++) {
                printf("      t[%d].conns[%d]:  pend=%u  rx_r=%u  tx_r=%u  cnt=%"
                        PRIu64" fd=%d\n",
                        i, j, cs[i].conns[j].pending, cs[i].conns[j].rx_remain,
                        cs[i].conns[j].tx_remain, cs[i].conns[j].cnt,
                        cs[i].conns[j].fd);
            }
        }
#endif

        fflush(stdout);
        memset(hist, 0, sizeof(*hist) * HIST_BUCKETS);

        t_prev = t_cur;
    }

#ifdef USE_MTCP
    mtcp_destroy();
#endif
}
