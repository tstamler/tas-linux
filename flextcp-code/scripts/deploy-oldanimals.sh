#!/bin/bash

host=rhino

RSYNC_EXCL="$(sed "s/^\\(.*\\)\$/--exclude '\\1'/" .gitignore) --exclude data/"

tag()
{
  sed -u "s/^/$1/"
}

run_tagged()
{
  pref="$1"
  shift
  { { "$@" ; } 2>&3 | tag "O $pref: " ; } 3>&1 1>&2 | tag "E $pref: "
}

echo $RSYNC_EXCL
rsync -av $RSYNC_EXCL . zookeeper:flextcp-code &
wait

if [[ "$1" == '-c' ]] ; then
  run_tagged $host ssh $host "cd flextcp-code && \
      make clean && cd mtcp && make clean" &
  wait
fi

run_tagged $host ssh $host "\
  cd flextcp-code && \
  make -j24 all && make -j24 flexkvs-linux && \
  cd mtcp && \
  make -j24 echoserver_linux echoserver_mtcp testclient_linux testclient_mtcp unidir_linux unidir_mtcp" &
wait

