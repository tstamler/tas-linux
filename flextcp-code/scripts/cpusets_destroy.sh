#!/bin/bash
sudo cset set -d /user/kernel
sudo cset set -d /user/flexnic
sudo cset set -d /user/application
sudo cset shield -r
echo 1 | tee /sys/devices/system/cpu/*/online >/dev/null
exit 0
