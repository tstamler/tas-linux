#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/nicutils.sh

dpdk_hugepages=1024
if [[ -z $1 ]]; then
  dpdk_path=$HOME/mtcp/dpdk-*
else
  dpdk_path="$1"
fi
clear_fn_shm
machine_if_info
dpdk_prepare
dpdk_bind_nic
if_config_ip
