#!/bin/bash

HOST=$1
TARGET=$2

if [ $TARGET = pagerank-flextcp ]; then
    PARALLELISM=-j1
else
    PARALLELISM=-j12
fi

ssh simon@$HOST "make -C flextcp clean >clean.log 2>&1; make $PARALLELISM -C flextcp $TARGET NTHREADS=$NTHREADS >build.log 2>&1 || (cat build.log; exit 1)"; echo "$?" >> /tmp/retcodes.tmp
