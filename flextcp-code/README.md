TCP Architecture
================

NIC Flow State
--------------
 * [32] Next segment number to receive
 * [64] Address of receive buffer
 * [32] Size of receive buffer
 * [32] First unused offset in receive buffer
 * [32] Last unused offset in receive buffer
 * [16] Number of packets received
 * [16] Number of ECE markings seen

 * [32] Next segment number to transmit
 * [64] Address of send buffer
 * [32] Size of send buffer
 * [32] First available offset in send buffer
 * [32] Last available offset in send buffer
 * [16] Number of packets sent


Packet Flow Receive
-------------------
 * If packet is TCP and lookup in flow table matches:
   - If ACK set, add ACK value to ack queue
   - If ECE (congestion experienced) set
     + Increment ECE counter
     + Add notification to kernel, passing packet and ECE counter

   - If packet has payload and packet is in order:
     + If packet is outside of receive buffer range: drop (or send NACK with)
     + Send ACK for this segement (also reflecting ECN marking if required)
     + Set up DMA to write payload to receive buffer
     + Set up DMA to write notification to app core notification queue
     + Update counters for sequence number to be received and first un-used offset in receive buffer

 * If packet is not TCP, is out of order, or FIN/RST set:
   - Set out of order flag for flow state
   - Add segment to kernel receive queue

Packet Flow Transmit
--------------------
In doorbell pipeline


Hardware Model
==============


Simulator
=========

FlexNIC Interface
-----------------
Definitions can be found in `include/flexnic.h`.
Interface consists of three types  of shared memory regions:
 * _info:_ contains `struct flexnic_info` which contains the parameters for the
    other regions. (Name macro: `FLEXNIC_NAME_INFO`)
 * _memory:_ memory for DMA. Size is described by `mem_size` parameter in info
    struct. (Name macro: `FLEXNIC_NAME_MEM`) 
 * _doorbells:_ queue for simulating writes to PCIe doorbell register. Number
    of available doorbell queues is described by `db_num` parameter in info
    struct, and the number of elements in each doorbell queue by `db_qlen`.
    Each entry in the doorbell queue is `FLEXNIC_DB_BYTES` bytes long. (Name
    macro: `FLEXNIC_NAME_DBS` for printf with an int parameter for queue id).


Building
========

You can use the deploy.sh script to deploy various benchmark applications on the swingout cluster.

graphlab
--------

To build for flextcp:

 * mv release-flextcp release
 * Uncomment #define FLEXNIC in src/graphlab/rpc/dc_tcp_comm.cpp
 * Uncomment dist_pagerank_flextcp lines in experimental/dist_pagerank/CMakeLists.txt
 * Comment dist_pagerank build line.

To build for Linux:

 * mv release-linux release
 * Do the reverse of the above

Setup
=====

The VFIO method is required for blocking network I/O. VFIO normally
requires a quite functional IOMMU setup, which not many servers
have. You can recompile the vfio.ko module with an option to disable
IOMMU support (it's default not built in most distributions due to
security issues). This allos you to say:

echo Y > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode

After which you should be able to bind a device to DPDK via VFIO.
