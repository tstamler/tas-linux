#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"

sudo killall kernel flexnic fastemu dummyem 2>/dev/null >/dev/null

sudo rm -f /dev/shm/flexnic_*
if [[ "$(hostname)" == "bigfish-e10k" ]]; then
  FN_CORES=4-7
else
  FN_CORES=8-11
fi
FN_CORES=0-1

rm -f /tmp/flexnic.log /tmp/kernel.log

# run flexnic and wait for ready
echo Launching flexnic
sudo $DIR/flexnic/flexnic --vdev=eth_pcap0,iface=veth0 -l $FN_CORES -n 2 -- \
  $FLEXNIC_ARGS 1 >/tmp/flexnic.log 2>&1 &
until grep "flexnic ready" /tmp/flexnic.log >/dev/null 2>/dev/null; do
  sleep 0.2
done

# run kernel and wait for ready
sleep 2
echo Launching kernel
sudo $DIR/kernel/kernel $KERNEL_ARGS 10.0.0.2 >/tmp/kernel.log 2>&1 &
until grep "kernel ready" /tmp/kernel.log >/dev/null 2>/dev/null; do
  sleep 0.2
done

# launch application
sleep 2
echo Launching application
sudo "$@"
echo Status: $?

sudo killall kernel
sudo killall flexnic
