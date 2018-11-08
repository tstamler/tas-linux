#!/bin/sh

if [ "x$1" = "x" ]; then
  1>&2 echo "Usage: limitcores.sh NUM-CORES"
  exit 1
fi

limit=$1
i=1
while [ -d /sys/devices/system/cpu/cpu${i} ]; do
  p=/sys/devices/system/cpu/cpu${i}
  if [ $i -lt $limit ]; then
    echo 1 | tee $p/online >/dev/null
  else
    echo 0 | tee $p/online >/dev/null
  fi
  i=$(($i+1))
done
exit 0
