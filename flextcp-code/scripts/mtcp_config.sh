#!/bin/sh
set -e
cat >/tmp/mtcp.conf <<EOF
io = dpdk
num_cores = $1
num_mem_ch = $2
port = dpdk0 dpdk1 ens2f1 enp180s0
max_concurrency = $3
max_num_buffers = $4
rcvbuf = $5
sndbuf = $6
EOF
