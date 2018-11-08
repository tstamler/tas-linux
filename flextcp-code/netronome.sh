#!/bin/bash

GATEWAY=zookeeper
HOST=lemur
NTHREADS=8
TARGET=all

echo Copying...

rsync -r --exclude=_add --exclude=*.log --exclude=logs --exclude=data --exclude=graphlabapi * simpeter@$GATEWAY:~/flextcp

echo Building...

ssh simpeter@$GATEWAY "ssh $HOST \"make -C flextcp clean >clean.log 2>&1; make -j12 -C flextcp $TARGET NTHREADS=$NTHREADS >build.log 2>&1 || (cat build.log; exit 1)\""; echo "$?" >> /tmp/retcodes.tmp
