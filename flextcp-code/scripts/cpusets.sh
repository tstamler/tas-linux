#!/bin/bash

echo 1 | tee /sys/devices/system/cpu/*/online >/dev/null

if [[ "$(hostname)" == "bigfish-e10k" ]] ; then
  sudo cset shield -c 1-15
  sudo cset shield -k on
  sudo cset set -m 0-7 user
  sudo cset set -m 0-7 -c 4-7 user/flexnic
  sudo cset set -m 0-7 -c 8-9 user/kernel
  sudo cset set -m 0-7 -c 1-3 user/application
elif [ "$(hostname)" == "rhinoceros" ] || [ "$(hostname)" == "sloth" ] ; then
  sudo cset shield -c 1-23
  sudo cset shield -k on
  sudo cset set -c 11-23 user/application
  sudo cset set -c 1-2 user/kernel
  sudo cset set -c 3-10 user/flexnic
elif [ "$(hostname)" == "fnhost" ] ; then
  sudo cset shield -c 1-23
  sudo cset shield -k on
  sudo cset set -c 15-23 user/application
  sudo cset set -c 1-2 user/kernel
  sudo cset set -c 3-14 user/flexnic
else
  sudo cset shield -c 1-7
  sudo cset shield -k on
  sudo cset set -c 5-6 user/flexnic
#  sudo cset set -c 6-7 user/kernel
  sudo cset set -c 7 user/kernel
  sudo cset set -c 1-4 user/application
fi
