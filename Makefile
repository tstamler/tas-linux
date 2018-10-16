CFLAGS = -std=gnu99 -g -Wall -Werror -I. -O2 -march=native 
LDFLAGS = -g
LDLIBS = -lrt
RTE_SDK=./dpdk

CFLAGS+= -I$(RTE_SDK)/include -I$(RTE_SDK)/include/dpdk
LDFLAGS+= -L$(RTE_SDK)/lib/
LIBS_DPDK= -Wl,--whole-archive
LIBS_DPDK+= -lrte_pmd_ixgbe -lrte_pmd_i40e  # IXGBE driver

LIBS_DPDK+= -lrte_eal -lrte_mempool \
	    -lrte_hash -lrte_ring -lrte_kvargs # DPDK Basics

LIBS_DPDK+= -lrte_ethdev
LIBS_DPDK+= -lrte_mbuf -lrte_pmd_ring
LIBS_DPDK+= -Wl,--no-whole-archive -ldl

LDLIBS += -lm -lrt -ldl

all: linux_test

linux_test.o: linux_test.c
	${CC} -c -o $@ $< ${CFLAGS}

linux_test: linux_test.o
	${CC} $< ${LIBS_DPDK} -o $@

clean:
	rm -f linux_test

