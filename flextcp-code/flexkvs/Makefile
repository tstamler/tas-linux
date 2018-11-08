FLEXKVS_COMMON=hashtable.o ialloc.o jenkins_hash.o settings.o
BENCH_COMMON=rng.o commandline.o workload.o

CFLAGS = -std=gnu99 -g -Wall -Icommon/include -I../include -I../stack/include -O3 -march=native
LDFLAGS = -pthread -g -O3
LDLIBS = -lrt -lm -lpthread -lrt -ldl

ROOT_DIR:=$(dir $(realpath $(lastword $(MAKEFILE_LIST))))


MTCP_BASE?=${HOME}/mtcp
DPDK_INC=${MTCP_BASE}/dpdk/include
DPDK_LIB=${MTCP_BASE}/dpdk/lib/
MTCP_INC    =-I${MTCP_BASE}/mtcp/include
MTCP_LIB    =-L${MTCP_BASE}/mtcp/lib

MTCP_CFLAGS+=${MTCP_INC} -DUSE_MTCP=1
MTCP_LDFLAGS+= ${MTCP_LIB}

DPDK_MACHINE_FLAGS = $(shell cat ${MTCP_BASE}/dpdk/include/cflags.txt)
MTCP_CFLAGS+= ${MTCP_INC} ${DPDK_MACHINE_FLAGS} -I${DPDK_INC} -include $(DPDK_INC)/rte_config.h

MTCP_LDLIBS += -Wl,-export-dynamic
MTCP_LDLIBS += -Wl,--start-group
MTCP_LDLIBS += -L${DPDK_LIB}
MTCP_LDLIBS += -Wl,-lmtcp
MTCP_LDLIBS += -Wl,-lnuma
MTCP_LDLIBS += -Wl,-lpthread
MTCP_LDLIBS += -Wl,-lrt
MTCP_LDLIBS += -Wl,-ldl
MTCP_LDLIBS += -Wl,--whole-archive
MTCP_LDLIBS += -Wl,-lrte_distributor
MTCP_LDLIBS += -Wl,-lrte_kni
MTCP_LDLIBS += -Wl,-lrte_pipeline
MTCP_LDLIBS += -Wl,-lrte_table
MTCP_LDLIBS += -Wl,-lrte_port
MTCP_LDLIBS += -Wl,-lrte_timer
MTCP_LDLIBS += -Wl,-lrte_hash
MTCP_LDLIBS += -Wl,-lrte_lpm
MTCP_LDLIBS += -Wl,-lrte_power
MTCP_LDLIBS += -Wl,-lrte_acl
MTCP_LDLIBS += -Wl,-lrte_meter
MTCP_LDLIBS += -Wl,-lrte_sched
MTCP_LDLIBS += -Wl,-lm
MTCP_LDLIBS += -Wl,-lrt
MTCP_LDLIBS += -Wl,-lrte_kvargs
MTCP_LDLIBS += -Wl,-lrte_mbuf
MTCP_LDLIBS += -Wl,-lrte_ip_frag
MTCP_LDLIBS += -Wl,-lethdev
MTCP_LDLIBS += -Wl,-lrte_mempool
MTCP_LDLIBS += -Wl,-lrte_ring
MTCP_LDLIBS += -Wl,-lrte_eal
MTCP_LDLIBS += -Wl,-lrte_cmdline
MTCP_LDLIBS += -Wl,-lrte_cfgfile
MTCP_LDLIBS += -Wl,-lrte_pmd_bond
MTCP_LDLIBS += -Wl,-lrte_pmd_vmxnet3_uio
MTCP_LDLIBS += -Wl,-lrte_pmd_i40e
MTCP_LDLIBS += -Wl,-lrte_pmd_ixgbe
MTCP_LDLIBS += -Wl,-lrte_pmd_e1000
MTCP_LDLIBS += -Wl,-lrte_pmd_ring
MTCP_LDLIBS += -Wl,--no-whole-archive
MTCP_LDLIBS += -Wl,-lrt
MTCP_LDLIBS += -Wl,-lm
MTCP_LDLIBS += -Wl,-ldl
MTCP_LDLIBS += -Wl,--end-group


all: flexkvs kvsbench flexkvs-flexnic kvsbench-flexnic flexkvs-mtcp \
		kvsbench-mtcp
all-sockets: flexkvs kvsbench
all-flexnic: flexkvs-flexnic kvsbench-flexnic
all-mtcp: flexkvs-mtcp kvsbench-mtcp

flexkvs: $(addprefix server/,$(FLEXKVS_COMMON) main.o)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

kvsbench: $(addprefix bench/,$(BENCH_COMMON) main.o) ../utils/utils.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

flexkvs-flexnic: $(addprefix server/,$(FLEXKVS_COMMON) main-flexnic.o) \
		$(ROOT_DIR)/../stack/flextcp.so
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

kvsbench-flexnic: $(addprefix bench/,$(BENCH_COMMON) main-flexnic.o) \
		$(ROOT_DIR)/../stack/flextcp.so
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

flexkvs-mtcp: $(addprefix server/,$(FLEXKVS_COMMON:.o=.mtcp.o) main.mtcp.o)
	$(CC) $(LDFLAGS) $(MTCP_LDFLAGS) -o $@ $^ $(LDLIBS) $(MTCP_LDLIBS)

kvsbench-mtcp: $(addprefix bench/,$(BENCH_COMMON:.o=.mtcp.o) main.mtcp.o) \
		../utils/utils.o
	$(CC) $(LDFLAGS) $(MTCP_LDFLAGS) -o $@ $^ $(LDLIBS) $(MTCP_LDLIBS)

%.mtcp.o: %.c
	$(CC) $(CFLAGS) $(MTCP_CFLAGS) -c -o $@ $<

clean:
	rm -f */*.o flexkvs kvsbench flexkvs-flexnic kvsbench-flexnic \
		flexkvs-mtcp kvsbench-mtcp

.PHONY: all all-sockets all-flexnic all-mtcp clean
