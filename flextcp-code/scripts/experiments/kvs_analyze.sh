#!/bin/bash

dir=$1

for cf in $dir/client_*.log ; do
  grep TP: $cf | sed -e '1,10d' | head -n 10
done
