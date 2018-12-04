TARGETS = echoserver_linux echoserver_mtcp \
	  testclient_linux testclient_mtcp \
	  unidir_linux unidir_mtcp
CC = gcc -g -O3 -Wall
DPDK=1
PS=0
NETMAP=0

MTCP_BASE?=${HOME}/mtcp

# DPDK LIBRARY and HEADER
DPDK_INC=${MTCP_BASE}/dpdk/include
DPDK_LIB=${MTCP_BASE}/dpdk/lib/

# mtcp library and header 
MTCP_FLD    =${MTCP_BASE}/mtcp
MTCP_INC    =-I${MTCP_FLD}/include
MTCP_LIB    =-L${MTCP_FLD}/lib
MTCP_TARGET = ${MTCP_LIB}/libmtcp.a

PS_DIR = ../../io_engine/
PS_INC = ${PS_DIR}/include
INC = -I./include/
INC_MTCP=${MTCP_INC}
LIBS_MTCP = ${MTCP_LIB}
ifeq ($(PS),1)
INC += -I{PS_INC}
LIBS_MTCP += -lmtcp -L${PS_DIR}/lib -lps -lpthread -lnuma -lrt
endif

ifeq ($(NETMAP),1)
LIBS_MTCP += -lmtcp -lpthread -lnuma -lrt
endif

# CFLAGS for DPDK-related compilation
INC += ${MTCP_INC}
ifeq ($(DPDK),1)
DPDK_MACHINE_FLAGS = $(shell cat  ${MTCP_BASE}/dpdk/include/cflags.txt)
INC_MTCP+= ${DPDK_MACHINE_FLAGS} -I${DPDK_INC} -include $(DPDK_INC)/rte_config.h
endif

LIBS += -m64 -g -O3 -pthread -lrt -march=native
ifeq ($(DPDK),1)
LIBS_MTCP += -Wl,-export-dynamic
LIBS_MTCP += -Wl,--start-group
LIBS_MTCP += -L${DPDK_LIB}
LIBS_MTCP += -Wl,-lmtcp
LIBS_MTCP += -Wl,-lnuma
LIBS_MTCP += -Wl,-lpthread
LIBS_MTCP += -Wl,-lrt
LIBS_MTCP += -Wl,-ldl
LIBS_MTCP += -Wl,--whole-archive
LIBS_MTCP += -Wl,-lrte_distributor
LIBS_MTCP += -Wl,-lrte_kni
LIBS_MTCP += -Wl,-lrte_pipeline
LIBS_MTCP += -Wl,-lrte_table
LIBS_MTCP += -Wl,-lrte_port
LIBS_MTCP += -Wl,-lrte_timer
LIBS_MTCP += -Wl,-lrte_hash
LIBS_MTCP += -Wl,-lrte_lpm
LIBS_MTCP += -Wl,-lrte_power
LIBS_MTCP += -Wl,-lrte_acl
LIBS_MTCP += -Wl,-lrte_meter
LIBS_MTCP += -Wl,-lrte_sched
LIBS_MTCP += -Wl,-lm
LIBS_MTCP += -Wl,-lrt
LIBS_MTCP += -Wl,-lrte_kvargs
LIBS_MTCP += -Wl,-lrte_mbuf
LIBS_MTCP += -Wl,-lrte_ip_frag
LIBS_MTCP += -Wl,-lethdev
LIBS_MTCP += -Wl,-lrte_mempool
LIBS_MTCP += -Wl,-lrte_ring
LIBS_MTCP += -Wl,-lrte_eal
LIBS_MTCP += -Wl,-lrte_cmdline
LIBS_MTCP += -Wl,-lrte_cfgfile
LIBS_MTCP += -Wl,-lrte_pmd_bond
LIBS_MTCP += -Wl,-lrte_pmd_vmxnet3_uio
LIBS_MTCP += -Wl,-lrte_pmd_i40e
LIBS_MTCP += -Wl,-lrte_pmd_ixgbe
LIBS_MTCP += -Wl,-lrte_pmd_e1000
LIBS_MTCP += -Wl,-lrte_pmd_ring
LIBS_MTCP += -Wl,--no-whole-archive
LIBS_MTCP += -Wl,-lrt
LIBS_MTCP += -Wl,-lm
LIBS_MTCP += -Wl,-ldl
LIBS_MTCP += -Wl,--end-group
endif

all: $(TARGETS)



echoserver_linux.o: echoserver.c
	${CC} -I../include -c -o $@ $< ${CFLAGS} ${INC}

echoserver_mtcp.o: echoserver.c
	${CC} -I../include -DUSE_MTCP=1 -c -o $@ $< ${CFLAGS} ${INC} ${INC_MTCP}


echoserver_linux: echoserver_linux.o ../utils/rng.o
	${CC} $^ ${LIBS} -o $@

echoserver_mtcp: echoserver_mtcp.o ../utils/rng.o
	${CC} $^ ${LIBS} ${LIBS_MTCP} -o $@


testclient_linux.o: testclient.c
	${CC} -c -o $@ $< ${CFLAGS} ${INC}

testclient_mtcp.o: testclient.c
	${CC} -DUSE_MTCP=1 -c -o $@ $< ${CFLAGS} ${INC} ${INC_MTCP}


testclient_linux: testclient_linux.o
	${CC} $< ${LIBS} -o $@

testclient_mtcp: testclient_mtcp.o
	${CC} $< ${LIBS} ${LIBS_MTCP} -o $@


unidir_linux.o: unidir.c
	${CC} -c -o $@ $< ${CFLAGS} ${INC}

unidir_mtcp.o: unidir.c
	${CC} -DUSE_MTCP=1 -c -o $@ $< ${CFLAGS} ${INC} ${INC_MTCP}

unidir_linux: unidir_linux.o
	${CC} $< ${LIBS} -o $@

unidir_mtcp: unidir_mtcp.o
	${CC} $< ${LIBS} ${LIBS_MTCP} -o $@

clean:
	rm -f *.o ${TARGETS}

distclean: clean
	rm -rf Makefile
