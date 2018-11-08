#!/bin/bash

machines="swingout1 swingout2 swingout3 swingout4 swingout5 fnhost"
RSYNC_EXCL="$(sed "s/^\\(.*\\)\$/--exclude '\\1'/" .gitignore)  --exclude data/"
#machines=swingout1


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

run_tagged_on()
{
  m="$1"
  shift
  run_tagged $m ssh $m "$*"
}

for m in $machines; do
  run_tagged $m rsync --delete -av $RSYNC_EXCL . $m:flextcp-code &
done
wait

cleancmd="true"
if [[ "$1" == '-c' ]] ; then
  cleancmd="make clean && make -C mtcp clean && make -C flexkvs clean"
fi

for m in $machines; do
  run_tagged_on $m "cd flextcp-code && $cleancmd && make -j12 all &&  make flexkvs-linux && cd mtcp && make clean && make -j8" &
done
wait
