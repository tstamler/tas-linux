HOSTNAME = $(shell hostname)
CFLAGS = -std=gnu99 -O3 -g -Wall -Werror -I. -Iinclude/ -march=native -DFLEXNIC -DHOSTNAME=\"$(HOSTNAME)\" -DHOSTNAME_$(subst -,_,$(HOSTNAME)) -DNTHREADS=$(NTHREADS)
LDFLAGS = -pthread -g
LDLIBS = -lrt

# lemur has the Netronome card
ifeq ($(HOSTNAME),lemur)
CFLAGS += -DNETRONOME
endif

# pig's compiler is too new - -O2 fails it working
ifneq ($(HOSTNAME),pig)
CFLAGS += -O2
endif

ifeq ($(MAKECMDGOALS),eval)
  ifeq ($(LOGFILE),)
    $(error Need to set LOGFILE)
  endif
endif

# swingout6 is a 32-bit machine
ifeq ($(HOSTNAME),swingout6)
RTE_SDK = $(HOME)/dpdk/i686-native-linuxapp-gcc
else
RTE_SDK = $(HOME)/dpdk/build
endif

CFLAGS+= -I$(RTE_SDK)/include -I$(RTE_SDK)/include/dpdk
LDFLAGS+= -L$(RTE_SDK)/lib/
LIBS_DPDK= -Wl,--whole-archive
ifeq ($(HOSTNAME),lemur)
LIBS_DPDK+= -lrte_pmd_nfp         # NFP driver
else
ifeq ($(HOSTNAME),deathstar)
LIBS_DPDK+= -lrte_pmd_pcap -lpcap # PCAP driver
else
LIBS_DPDK+= -lrte_pmd_ixgbe -lrte_pmd_i40e  # IXGBE driver
endif
endif
LIBS_DPDK+= -lrte_eal -lrte_mempool -lrte_mempool_ring \
	    -lrte_hash -lrte_ring -lrte_kvargs # DPDK Basics
ifeq ($(HOSTNAME),deathstar) # handle newer dpdk version
LIBS_DPDK+= -lrte_ethdev
else
LIBS_DPDK+= -lrte_ethdev
endif
LIBS_DPDK+= -lrte_mbuf -lnuma -lrte_bus_pci -lrte_pci
LIBS_DPDK+= -Wl,--no-whole-archive -ldl

LDLIBS += -lm -lpthread -lrt -ldl

UTILS_OBJS = $(addprefix utils/,utils.o rng.o timeout.o)
DRIVER_OBJS = $(addprefix driver/,driver.o)
KERNEL_OBJS = $(addprefix kernel/,kernel.o tap.o packetmem.o appif.o appif_ctx.o \
	nicif.o cc.o tcp.o arp.o config.o routing.o)
FASTEMU_OBJS = $(addprefix flexnic/,fastemu.o flexnic.o dma.o network.o \
		    qman.o trace.o fast_kernel.o fast_appctx.o fast_flows.o)
DUMMYEM_OBJS = $(addprefix flexnic/,dummyem.o flexnic.o dma.o network.o \
		    qman.o trace.o)
STACK_OBJS = $(addprefix stack/,init.o kernel.o conn.o)
SOCKETS_OBJS = $(addprefix sockets/,control.o transfer.o context.o manage_fd.o \
	epoll.o)
INTERPOSE_OBJS = $(addprefix sockets/,interpose.o)
CFLAGS += -I. -Istack/include -Isockets/include

shared_objs = $(patsubst %.o,%.shared.o,$(1))

all: tests/lowlevel tests/lowlevel_echo \
	tests/usocket_accept tests/usocket_connect tests/usocket_accrx \
	tests/usocket_conntx tests/usocket_conntx_large tests/usocket_move \
	tests/bench_ll_echo tests/obj_ll_echo tests/obj_ll_bench \
	mtcp/unidir_ll mtcp/router \
	sockets/flextcp_sockets.so sockets/flextcp_interpose.so \
	stack/flextcp.so tracetool statetool scaletool flexnic/splittcp

docs:
	cd doc && doxygen

startall: all
	./startall.sh

build:
	./deploy.sh build

deploy:
	./deploy.sh | tee deploy.log

eval:
	./eval.sh $(LOGFILE)

flexnic/fastemu: LDLIBS+=$(LIBS_DPDK)
flexnic/fastemu: $(FASTEMU_OBJS) $(UTILS_OBJS)

flexnic/splittcp: LDLIBS+=$(LIBS_DPDK)
flexnic/splittcp: $(FASTEMU_OBJS:flexnic.o=splittcp.o) $(KERNEL_OBJS:kernel.o=kernel_splittcp.o) $(DRIVER_OBJS) $(UTILS_OBJS)

flexnic/splittcp.o: flexnic/splittcp.c flexnic/flexnic.c
kernel/kernel_splittcp.o: kernel/kernel_splittcp.c kernel/kernel.c

flexnic/dummyem: LDLIBS+=$(LIBS_DPDK)
flexnic/dummyem: $(DUMMYEM_OBJS) $(UTILS_OBJS)

kernel/kernel: LDLIBS+=-lrte_eal
kernel/kernel: $(KERNEL_OBJS) $(DRIVER_OBJS) $(UTILS_OBJS)

flexnic/tests/tcp_common: flexnic/tests/tcp_common.o

tests/lowlevel: tests/lowlevel.o $(STACK_OBJS) $(DRIVER_OBJS) $(UTILS_OBJS)
tests/lowlevel_echo: tests/lowlevel_echo.o $(STACK_OBJS) $(DRIVER_OBJS) $(UTILS_OBJS)
tests/obj_ll_echo: tests/obj_ll_echo.o $(STACK_OBJS) $(DRIVER_OBJS) $(UTILS_OBJS)
tests/obj_ll_bench: tests/obj_ll_bench.o $(STACK_OBJS) $(DRIVER_OBJS) $(UTILS_OBJS)

tests/usocket_accept: tests/usocket_accept.o sockets/flextcp_sockets.so
tests/usocket_connect: tests/usocket_connect.o sockets/flextcp_sockets.so
tests/usocket_accrx: tests/usocket_accrx.o sockets/flextcp_sockets.so
tests/usocket_conntx: tests/usocket_conntx.o sockets/flextcp_sockets.so
tests/usocket_conntx_large: tests/usocket_conntx_large.o \
	sockets/flextcp_sockets.so
tests/usocket_move: tests/usocket_move.o sockets/flextcp_sockets.so
tests/bench_ll_echo: tests/bench_ll_echo.o $(STACK_OBJS) $(DRIVER_OBJS) \
	$(UTILS_OBJS)

tracetool: tracetool.o
statetool: statetool.o $(DRIVER_OBJS)
scaletool: scaletool.o $(STACK_OBJS) $(DRIVER_OBJS) $(UTILS_OBJS)

sockets/flextcp_sockets.so: $(call shared_objs, \
	$(SOCKETS_OBJS) $(STACK_OBJS) $(DRIVER_OBJS) $(UTILS_OBJS))

sockets/flextcp_interpose.so: $(call shared_objs, \
	$(INTERPOSE_OBJS) $(SOCKETS_OBJS) $(STACK_OBJS) $(DRIVER_OBJS) $(UTILS_OBJS))

stack/flextcp.so: $(call shared_objs, \
	$(STACK_OBJS) $(DRIVER_OBJS) $(UTILS_OBJS))

%.shared.o: %.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

%.so:
	$(CC) $(LDFLAGS) -shared $^ $(LOADLIBES) $(LDLIBS) -o $@

mtcp/unidir_ll: mtcp/unidir_ll.o $(STACK_OBJS) $(DRIVER_OBJS) $(UTILS_OBJS)

mtcp/router: LDLIBS+=$(LIBS_DPDK)
mtcp/router: mtcp/router.o $(UTILS_OBJS)

tcpecho: tcpecho.o $(FLEXTCP_OBJS)

tcptest: tcptest.o $(FLEXTCP_OBJS)

tcptest_async: tcptest_async.o $(FLEXTCP_OBJS)

ftrans: ftrans.o $(FLEXTCP_OBJS)


clean:
	rm -f *.o flexnic/*.o kernel/*.o utils/*.o stack/*.o sockets/*.o \
	  tests/*.o driver/*.o mtcp/*.o kernel/kernel \
	  sockets/flextcp_sockets.so sockets/flextcp_interpose.so \
	  stack/flextcp.so \
	  tests/lowlevel tests/lowlevel_echo tests/usocket_accept \
	  tests/usocket_connect tests/usocket_accrx tests/usocket_conntx \
	  tests/usocket_conntx_large tests/usocket_move tests/obj_ll_echo \
	  tests/obj_ll_bench mtcp/unidir_ll mtcp/router \
	  tcpecho tcptest tcptest_async flextcp.so tracetool statetool \
	  scaletool flexnic/fastemu flexnic/dummyem flexnic/splittcp
	rm -f $(FLEXTCP_OBJS) $(FLEXTCP_SHARED_OBJS)
	$(MAKE) -C flexkvs clean
# $(MAKE) -C memcached-1.4.25 clean

.PHONY: flexkvs-flextcp-obj flexkvs-linux memcached-linux memcached-flextcp \
  pagerank-flextcp all clean docs
flexkvs-flextcp-obj:
	$(MAKE) -C flexkvs COMPFLAGS="-DLINUX_SOCKETS=0 -DFLEXTCP_OBJ=1"

flexkvs-linux:
	$(MAKE) -C flexkvs COMPFLAGS="-DLINUX_SOCKETS=1"

memcached-linux: flextcp.so flexnic kernel
ifeq ($(HOSTNAME),swingout6)
	cd memcached-1.4.25 && autoreconf && ./configure
else
	cd memcached-1.4.25 && autoreconf && ./configure --enable-64bit
endif
	$(MAKE) -C memcached-1.4.25
	$(MAKE) -C memc_benchmark_client

memcached-flextcp: flextcp.so flexnic kernel
	cd memcached-1.4.25 && ./configure --enable-64bit CPPFLAGS="-DNDEBUG -DFLEXNIC" LIBS="$(HOME)/flextcp/flextcp.so -lrt -ldl"
	$(MAKE) -C memcached-1.4.25 memcached
	$(MAKE) -C memc_benchmark_client

pagerank-flextcp: all
	ln -sf flextcp.so libflextcp.so
	ln -sf flextcp_sockets.so sockets/libflextcp_sockets.so
	ln -sf flextcp_interpose.so sockets/libflextcp_interpose.so
	$(MAKE) -j1 -C graphlabapi/release/experimental/dist_pagerank
