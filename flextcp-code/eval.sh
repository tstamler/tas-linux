#!/bin/bash

# Get throughput and min/avg/max latencies
NLINES=`wc -l $1 | cut -f1 -d' '`
./eval.awk -v NLINES=$NLINES $1 2>latencies.dat

# Get median and 99th percentile latencies
sort -g latencies.dat > latencies_sorted.dat
NLINES=`wc -l latencies_sorted.dat | cut -f1 -d' '`
./median.awk -v NLINES=$NLINES latencies_sorted.dat
