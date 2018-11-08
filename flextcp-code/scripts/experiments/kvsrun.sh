#!/bin/bash

server=so1
clients="so2 so3 so4 so5"

myhost()
{
  case $1 in
    so1)
      echo swingout1-brcm1.pvt
      ;;
    so2)
      echo swingout2-brcm1.pvt
      ;;
    so3)
      echo swingout3-brcm1.pvt
      ;;
    so4)
      echo swingout4
      ;;
    so5)
      echo swingout5
      ;;
    *)
      exit -1
      ;;
  esac
}

myip()
{
  case $1 in
    so1)
      echo 10.0.0.1
      ;;
    so2)
      echo 10.0.0.2
      ;;
    so3)
      echo 10.0.0.3
      ;;
    so4)
      echo 10.0.0.4
      ;;
    so5)
      echo 10.0.0.5
      ;;
    *)
      exit -1
      ;;
  esac
}

tag()
{
  sed -u "s/^/$1/"
}

die()
{
  (>&2 echo "$@")
  exit -1
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
  h="$(myhost $m)"
  shift
  run_tagged $m ssh $h "$*"
}


if [[ "$1" == "" ]] ; then
  die Usage: kvsrun.sh DIR
fi
outdir_base=$1
if [[ -e $outdir_base ]] ; then
  die DIR already exists
fi
mkdir -p $outdir_base || die Creating outdir_base failed

for m in $server $clients ; do
  run_tagged_on $m "sudo killall flexnic kernel flexkvs flexkvs-flexnic kvsbench kvsbench-flexnic flexnic_app.sh ; cd flextcp-code ; sudo scripts/cpusets_destroy.sh" &>/dev/null &
done
wait

sleep 2

kvs_cores=1
kv_threads=1
kv_conns=1
kvs_param="-v 64 -k 32 -n 100000 -z 0.90"
server_ip=$(myip $server)

for stack in mtcp ; do
#for stack in linux mtcp flextcp obj; do
  for m in $server $clients ; do
    case $stack in
      flextcp|obj)
        run_tagged_on $m "cd flextcp-code && scripts/dpdk_init.sh" &
        ;;
      linux)
        run_tagged_on $m "cd flextcp-code && scripts/linux_stack.sh" &
        ;;
      mtcp)
        run_tagged_on $m "cd flextcp-code && scripts/mtcp_init.sh" &
        ;;
    esac
  done
  wait
  sleep 1

#for fn_cores in 4 2 1 ; do
#for kvs_cores in 1 4 2; do
#for kv_conns in 1 2 4 8; do
for fn_cores in 4 ; do
for kvs_cores in 1; do
for kv_conns in 4; do

  echo stack=$stack fn_cores=$fn_cores kvs_cores=$kvs_cores kv_conns=$kv_conns

  outdir=$outdir_base/conns_stack${stack}_fc${fn_cores}_sc${kvs_cores}_cc${kv_conns}
  mkdir -p $outdir || die Creating outdir failed

  case $stack in
    flextcp)
      run_tagged_on $server "cd flextcp-code && IP=$server_ip FLEXNIC_THREADS=$fn_cores scripts/flexnic_app.sh env LD_PRELOAD=sockets/flextcp_interpose.so flexkvs/flexkvs cfg $kvs_cores" &>$outdir/server.log &
      ;;
    obj)
      run_tagged_on $server "cd flextcp-code && IP=$server_ip FLEXNIC_THREADS=$fn_cores scripts/flexnic_app.sh flexkvs/flexkvs-flexnic cfg $kvs_cores" &>$outdir/server.log &
      ;;
    mtcp)
      run_tagged_on $server "cd flextcp-code/flexkvs-mtcp && sed -i -e \"s/num_cores =.*/num_cores = $kvs_cores/g\" flexkvs.conf && sudo ./flexkvs flexkvs.conf $kvs_cores" &>$outdir/server.log &
      ;;
    linux)
      run_tagged_on $server "cd flextcp-code && flexkvs/flexkvs cfg $kvs_cores" &>$outdir/server.log &
      ;;
  esac

  until grep "Starting maintenance" $outdir/server.log >/dev/null 2>/dev/null; do
    sleep 0.2
  done

  sleep 1

  for m in $clients ; do
    ip=$(myip $m)
    case $stack in
      flextcp)
        run_tagged_on $m "cd flextcp-code && IP=$ip FLEXNIC_THREADS=$fn_cores scripts/flexnic_app.sh env LD_PRELOAD=sockets/flextcp_interpose.so flexkvs/kvsbench ${server_ip}:11211 $kv_params -t $kv_threads -C $kv_conns -p 1" &>$outdir/client_${m}.log &
        ;;
      obj)
        run_tagged_on $m "cd flextcp-code && IP=$ip FLEXNIC_THREADS=$fn_cores scripts/flexnic_app.sh flexkvs/kvsbench-flexnic ${server_ip}:11211 $kv_params -t $kv_threads -C $kv_conns -p 1" &>$outdir/client_${m}.log &
        ;;
      mtcp)
        run_tagged_on $m "cd flextcp-code/flexkvs-mtcp && sed -i -e \"s/num_cores =.*/num_cores = $kv_threads/g\" kvsbench.conf && sudo ./kvsbench ${server_ip}:11211 $kv_params -t $kv_threads -C $kv_conns -p 1" &>$outdir/client_${m}.log &
        ;;
      linux)
        run_tagged_on $m "cd flextcp-code && flexkvs/kvsbench ${server_ip}:11211 $kv_params -t $kv_threads -C $kv_conns -p 1" &>$outdir/client_${m}.log &
        ;;
    esac
  done

  sleep 45

  for m in $clients $server ; do
    run_tagged_on $m "sudo killall kvsbench kvsbench-flexnic flexkvs flexkvs-flexnic kernel flexnic flexnic_app.sh ; cd flextcp-code ; sudo scripts/cpusets_destroy.sh" &>/dev/null &
  done
  wait
  sleep 2
done
done
done
done
