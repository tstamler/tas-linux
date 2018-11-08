#!/bin/bash

server=so1
clients="so2 so3 so4 so5"

myhost()
{
  case $1 in
    rhino)
      echo 10.100.1.19
      ;;
    sloth)
      echo 10.100.1.20
      ;;
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
    rhino)
      echo 10.0.0.10
      ;;
    sloth)
      echo 10.0.0.11
      ;;
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
  run_tagged_on $m "sudo killall flexnic kernel echoserver_linux echoserver_mtcp testclient_linux testclient_mtcp flexnic_app.sh ; cd flextcp-code ; sudo -A scripts/cpusets_destroy.sh" &>/dev/null &
done
wait

sleep 2

server_ip=$(myip $server)
server_cores=1


#for stack in linux mtcp flextcp; do
for stack in mtcp; do
  for m in $server $clients ; do
    case $stack in
      flextcp)
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

for fn_cores in 4 ; do
#for client_cores in 1 2 4; do
for client_cores in 1; do
  if [[ "$client_cores" == "4" ]]; then
    connset="1 2 4 8 16 32 64"
  else
    connset="1"
  fi
for client_conns in $connset; do
#for fn_cores in 4 ; do
#for client_cores in 4 ; do
#for client_conns in 1 ; do

  echo stack=$stack fn_cores=$fn_cores client_cores=$client_cores client_conns=$client_conns

  outdir=$outdir_base/conns_stack${stack}_fc${fn_cores}_co${client_cores}_cc${client_conns}
  mkdir -p $outdir || die Creating outdir failed

  case $stack in
    flextcp)
      run_tagged_on $server "cd flextcp-code && IP=$server_ip FLEXNIC_THREADS=$fn_cores scripts/flexnic_app.sh env LD_PRELOAD=sockets/flextcp_interpose.so mtcp/echoserver_linux 12345 $server_cores echoserver.conf 1024 64" &>$outdir/server.log &
      ;;
    mtcp)
      run_tagged_on $server "cd flextcp-code/mtcp && sed -i -e \"s/num_cores =.*/num_cores = $server_cores/g\" echoserver.conf && sudo ./echoserver_mtcp 12345 $server_cores echoserver.conf 1024 64" &>$outdir/server.log &
      ;;
    linux)
      run_tagged_on $server "cd flextcp-code && mtcp/echoserver_linux 12345 $server_cores echoserver.conf 1024 64" &>$outdir/server.log &
      ;;
  esac

  until grep "Workers ready" $outdir/server.log >/dev/null 2>/dev/null; do
    sleep 0.2
  done

  sleep 1

  for m in $clients ; do
    ip=$(myip $m)
    case $stack in
      flextcp)
        run_tagged_on $m "cd flextcp-code && IP=$ip FLEXNIC_THREADS=$fn_cores scripts/flexnic_app.sh env LD_PRELOAD=sockets/flextcp_interpose.so mtcp/testclient_linux ${server_ip} 12345 $client_cores testclient.conf 64 1 $client_conns" &>$outdir/client_${m}.log &
        ;;
      mtcp)
        run_tagged_on $m "cd flextcp-code/mtcp && sed -i -e \"s/num_cores =.*/num_cores = $client_cores/g\" testclient.conf && sudo ./testclient_mtcp ${server_ip} 12345 $client_cores testclient.conf 64 1 $client_conns" &>$outdir/client_${m}.log &
        ;;
      linux)
        run_tagged_on $m "cd flextcp-code && mtcp/testclient_linux ${server_ip} 12345 $client_cores testclient.conf 64 1 $client_conns" &>$outdir/client_${m}.log &
        ;;
    esac
  done

  sleep 200

  for m in $clients $server ; do
    run_tagged_on $m "sudo killall echoserver_linux echoserver_mtcp testclient_linux testclient_mtcp kernel flexnic flexnic_app.sh ; cd flextcp-code ; sudo scripts/cpusets_destroy.sh" &>/dev/null &
  done
  wait
  sleep 2
done
done
done
done
