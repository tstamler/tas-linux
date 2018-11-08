#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"

if [[ "x$IP" == "x" ]]; then
  >&2 echo "IP environment variable not set"
  exit -1
fi

if [[ "x$FLEXNIC_THREADS" == "x" ]]; then
  FLEXNIC_THREADS=1
fi


sudo killall kernel flexnic fastemu dummyem splittcp 2>/dev/null >/dev/null

sudo rm -f /dev/shm/flexnic_*
sudo rm -f /mnt/huge/rtemap_* /mnt/huge/flexnic_*
if [[ "$(hostname)" == "bigfish-e10k" ]]; then
  FN_CORES=4-7
  MEM_CHANNELS=2
elif [ "$(hostname)" == "rhinoceros" ] || [ "$(hostname)" == "sloth" ] ; then
  FN_CORES=3-7
  MEM_CHANNELS=4
else
  FN_CORES=5-6
  MEM_CHANNELS=2
fi

rm -f /tmp/flexnic.log /tmp/kernel.log /tmp/flexnic.pid /tmp/kernel.pid

# $DIR/scripts/cpusets.sh

# FASTEMU=fastemu
FASTEMU=splittcp

# run flexnic and wait for ready
if [[ "x$FASTEMU" == "x" ]]; then
  fn_name=flexnic
elif [[ "$FASTEMU" == "dummy" ]]; then
  fn_name=dummyem
elif [[ "$FASTEMU" == "splittcp" ]]; then
  fn_name=splittcp
else
  fn_name=fastemu
fi
echo Launching $fn_name
# sudo cset proc -s user/flexnic -e -- $DIR/flexnic/$fn_name -l $FN_CORES \
#     -n $MEM_CHANNELS --vdev=net_tap -- $FLEXNIC_ARGS $FLEXNIC_THREADS >/tmp/flexnic.log 2>&1 &
if [[ "$fn_name" != "splittcp" ]]; then
    sudo $DIR/flexnic/$fn_name -l $FN_CORES -n $MEM_CHANNELS -- $FLEXNIC_ARGS $FLEXNIC_THREADS >/tmp/flexnic.log 2>&1 &

    until grep "flexnic ready" /tmp/flexnic.log >/dev/null 2>/dev/null; do
	sleep 0.2
    done
else
    sudo $DIR/flexnic/$fn_name -l $FN_CORES -n $MEM_CHANNELS -- $FLEXNIC_ARGS $FLEXNIC_THREADS $KERNEL_ARGS --ip-addr=$IP >/tmp/flexnic.log 2>&1 &

    until grep "kernel ready" /tmp/flexnic.log >/dev/null 2>/dev/null; do
	sleep 0.1
    done
fi

if [[ "$fn_name" != "splittcp" ]]; then
    echo `pgrep $fn_name` > /tmp/flexnic.pid

    # run kernel and wait for ready
    sleep 2
    echo Launching kernel
    # sudo cset proc -s user/kernel -e -- $DIR/kernel/kernel $KERNEL_ARGS $IP \
    #   >/tmp/kernel.log 2>&1 &
    sudo $DIR/kernel/kernel $KERNEL_ARGS --ip-addr=$IP >/tmp/kernel.log 2>&1 &
    until grep "kernel ready" /tmp/kernel.log >/dev/null 2>/dev/null; do
	sleep 0.2
    done
    echo `pgrep kernel` > /tmp/kernel.pid

    sleep 2
fi

# launch application
echo Launching application
# sudo cset proc -s user/application -e -- "$@"
sudo "$@"
echo Status: $?

sudo killall kernel flexnic fastemu dummyen splittcp 2>/dev/null >/dev/null

# $DIR/scripts/cpusets_destroy.sh
