#!/bin/bash

set -e

# swingout1-brcm1 and bigfish-e1k1 only accessible to DPDK

#ALLHOSTS=("swingout5")
#ALLHOSTS=("swingout5" "swingout4")

#ALLHOSTS=("128.208.6.130" "128.208.6.129" "128.208.6.175")
#ALLHOSTS=("10.0.0.5" "10.0.0.4" "10.0.0.2")
ALLHOSTS=("10.0.0.5" "10.0.0.4" "10.0.0.1")
#ALLHOSTS=("10.0.0.5" "10.0.0.4")
#ALLHOSTS=("128.208.6.130" "128.208.6.129")
#ALLHOSTS=("10.0.0.5")
# ALLHOSTS=("128.208.6.130" "128.208.6.129")
ALLHOSTS_DPDK=("128.208.6.128" "128.208.6.149" "128.208.6.175")
#ALLHOSTS_DPDK=("128.208.6.128" "128.208.6.149")

# HOSTS="swingout5 swingout4 swingout6 bigfish-e1k1 swingout1-brcm1"
# INITIATOR="swingout4 swingout6 bigfish-e1k1 swingout1-brcm1"
# HOSTS="swingout5 swingout4 swingout6 swingout1"
# INITIATOR="swingout4 swingout6 swingout1"
#HOSTS="swingout5 swingout4 swingout2-brcm1"
#HOSTS="swingout5"
HOSTS="swingout5 swingout4 swingout1-brcm1.pvt"
#HOSTS="swingout5 swingout4"
INITIATOR="swingout4"
NTHREADS=8
THREADS_PER_INITIATOR=4
MEMCACHED_THREADS=2

CLEANLIST="flexnic.h kernel.h pipeline.h ratelimit.h"

export NTHREADS
declare -A BONDING

# swingout3
BONDING["192.168.26.8"]="--vdev 'eth_bond0,mode=0,slave=0000:0c:00.0,slave=0000:0c:00.1'"
# swingout5
BONDING["192.168.26.20"]="--vdev 'eth_bond0,mode=0,slave=0000:08:00.0,slave=0000:08:00.1'"

buildall ()
{
    echo Copying to $HOSTS...

# Build all
    for h in $HOSTS; do
	ssh simon@$h "cd flextcp; rm -f $CLEANLIST"
	rsync -u -r --exclude=_add --exclude=*.log --exclude=logs --exclude=data * simon@$h:~/flextcp &
    done

    wait

    echo Building on $HOSTS...

    rm -f /tmp/retcodes.tmp

    for h in $HOSTS; do
	./build.sh $h $1 &
    done

    # for h in $INITIATOR; do
    # 	./build.sh $h testsuite &
    # done

    wait
    grep -qv 0 /tmp/retcodes.tmp && exit 1
    echo done
}

deployflexnic ()
{
# Load flexnic process if FLEXNIC is part of config
    echo "FlexNIC deployment on $HOSTS"
    for h in $HOSTS; do
	case $h in
	    swingout6)
		ssh -q -tt simon@$h "sudo rm -f /dev/shm/flexnic_*; ulimit -c unlimited; sudo ./flextcp/flexnic -l 4-7 -n 2 -- $h 2>&1" &
		;;
	    *)
		ssh -q -tt simon@$h "sudo rm -f /dev/shm/flexnic_*; ulimit -c unlimited; sudo ./flextcp/flexnic -l 6-11 -n 2 -- $h 2>&1" &
		# ssh -q -tt simon@$h "sudo rm -f /dev/shm/flexnic_inq_* /dev/shm/flexnic_outq_*; ulimit -c unlimited; sudo ./flextcp/flexnic -l 6-11 -n 2 -- $h 2>&1" &
		    # ssh -q -tt simon@$h "sudo rm -f /dev/shm/worker_*_inq_* /dev/shm/worker_*_outq_*; ulimit -c unlimited; sudo ./mystorm/flexnic -l 6-11 -n 2 ${BONDING[$h]} -- $h 2>&1" &
		;;
	esac
    done

    sleep 3

# Load flexnic kernel process
    echo "Kernel deployment on $HOSTS"
    for h in $HOSTS; do
	case $h in
	    swingout6)
		ssh -q -tt simon@$h "ulimit -c unlimited; sudo taskset -a -c 3 ./flextcp/kernel 2>&1" &
		;;
	    *)
		ssh -q -tt simon@$h "ulimit -c unlimited; sudo taskset -a -c 4-5 ./flextcp/kernel 2>&1" &
		;;
	esac
    done

    sleep 1
}

deployall ()
{
    echo Deploying tcpecho on ${ALLHOSTS[*]}...

    deployflexnic

# Load all server apps
    for ((i=0; i<${#ALLHOSTS[@]}; i++)); do
	case ${ALLHOSTS[$i]} in
	    *)
		ssh -q -tt simon@${ALLHOSTS[$i]} "ulimit -c unlimited; sudo taskset -a -c 0-3 ./flextcp/tcpecho 2>&1" &
		;;
	esac
    done
}

deploylinux ()
{
    echo Deploying on ${ALLHOSTS[*]}...

    # scp -q -r * simon@$INITIATOR:~/flextcp

# Load flexnic process if FLEXNIC is part of config
    echo "Linux deployment on ${ALLHOSTS[@]}"

# Load all server apps
    for ((i=0; i<${#ALLHOSTS[@]}; i++)); do
	case ${ALLHOSTS[$i]} in
	    *)
		ssh -q -tt simon@${ALLHOSTS[$i]} "ulimit -c unlimited; ./flextcp/tcpecho.linux 2>&1" &
		;;
	esac
    done
}

deployflexkvs ()
{
    echo Deploying FlexKVS on ${ALLHOSTS[*]}...

    deployflexnic

# Load all server apps
    for ((i=0; i<${#ALLHOSTS[@]}; i++)); do
	case ${ALLHOSTS[$i]} in
	    *)
		ssh -q -tt simon@${ALLHOSTS[$i]} "ulimit -c unlimited; sudo taskset -a -c 0-3 ./flextcp/flexkvs/flexkvs 2>&1" &
		;;
	esac
    done
}

deployflexkvslinux ()
{
    echo Deploying FlexKVS-Linux on ${ALLHOSTS[*]}...

# Load all server apps
    for ((i=0; i<${#ALLHOSTS[@]}; i++)); do
	case ${ALLHOSTS[$i]} in
	    *)
		ssh -q -tt simon@${ALLHOSTS[$i]} "ulimit -c unlimited; sudo ./flextcp/flexkvs/flexkvs 2>&1" &
		;;
	esac
    done
}

deploymemcachedlinux ()
{
    echo Deploying memcached-Linux on ${ALLHOSTS[*]}...

# Load all server apps
    for ((i=0; i<${#ALLHOSTS[@]}; i++)); do
	case ${ALLHOSTS[$i]} in
	    *)
		ssh -q -tt simon@${ALLHOSTS[$i]} "ulimit -c unlimited; ./flextcp/memcached-1.4.25/memcached -t $MEMCACHED_THREADS 2>&1" &
		;;
	esac
    done
}

deploymemcachedflextcp ()
{
    echo Deploying memcached-FlexTCP on ${ALLHOSTS[*]}...

# Load all server apps
    for ((i=0; i<${#ALLHOSTS[@]}; i++)); do
	case ${ALLHOSTS[$i]} in
	    *)
		ssh -q -tt simon@${ALLHOSTS[$i]} "ulimit -c unlimited; ./flextcp/memcached-1.4.25/memcached -t $MEMCACHED_THREADS -l 128.208.6.128 -U 0 -p 11211 2>&1" &
		;;
	esac
    done
}

deploypagerankflextcp ()
{
    echo Deploying pagerank-FlexTCP on $HOSTS...

    OLDIFS=$IFS
    IFS=,
    allmachines="${ALLHOSTS_DPDK[*]}"
    IFS=$OLDIFS

# Load all server apps
    i=0
    for h in $HOSTS; do
    # for ((i=0; i<${#ALLHOSTS[@]}; i++)); do
    # 	case ${ALLHOSTS[$i]} in
    # 	    *)
	ssh -q -tt simon@$h "ulimit -c unlimited; cd flextcp/graphlabapi/release/experimental/dist_pagerank; export HOSTNAME; IP=${ALLHOSTS_DPDK[$i]} /home/simon/flextcp/scripts/flexnic_app.sh env SPAWNNODES=\"$allmachines\" SPAWNID=$i ./dist_pagerank_flextcp --graph atom.idx --ncpus 2 2>&1" &
		# ;;
	# esac
	i=$((i + 1))
    done
}

deploypageranklinux ()
{
    echo Deploying pagerank-Linux on $HOSTS...

    OLDIFS=$IFS
    IFS=,
    allmachines="${ALLHOSTS[*]}"
    IFS=$OLDIFS

# Load all server apps
    i=0
    for h in $HOSTS; do
	ssh -q -tt simon@$h "ulimit -c unlimited; cd flextcp/graphlabapi/release/experimental/dist_pagerank; export HOSTNAME; sudo cset proc -s user/application -e -- env SPAWNNODES=\"$allmachines\" SPAWNID=$i ./dist_pagerank --graph atom.idx --ncpus 2 2>&1" &
	# ssh -q -tt simon@$h "ulimit -c unlimited; cd flextcp/graphlabapi/release/experimental/dist_pagerank; export HOSTNAME; IP=${ALLHOSTS_DPDK[$i]} /home/simon/flextcp/scripts/flexnic_app.sh env SPAWNNODES=\"$allmachines\" SPAWNID=$i ./dist_pagerank --graph atom.idx --ncpus 4 2>&1" &
	i=$((i + 1))
    done
}

startall ()
{
    # ssh simon@$INITIATOR "./flextcp/tcptest.sh" &
    # ssh simon@$INITIATOR "./flextcp/tcptest 128.208.6.128 1234 > tcptest.log" &
#    ssh -q -tt simon@$INITIATOR "ulimit -c unlimited; sudo taskset -a -c 0-3 ./flextcp/tcptest 128.208.6.128 1234 >tcptest.log 2>&1" &
    # ssh -q -tt simon@$INITIATOR "ulimit -c unlimited; sudo taskset -a -c 0-3 ./flextcp/tcptest 128.208.6.128 1234 >tcptest.log" &
    for h in $INITIATOR; do
	case $h in
	    swingout6)
		ssh -q -tt simon@$h "ulimit -c unlimited; sudo rm ~/tcptest_*.log; sudo taskset -a -c 0-2 ./flextcp/tcptest_async 128.208.6.128 1234" &
		;;
	    *)
		ssh -q -tt simon@$h "ulimit -c unlimited; sudo rm ~/tcptest_*.log; sudo taskset -a -c 0-3 ./flextcp/tcptest_async 128.208.6.128 1234" &
		;;
	esac
    done
}

startlinux ()
{
    # Cleanup all logs
    for h in $INITIATOR; do
	ssh -q simon@$h "sudo rm -f ~/tcptest_*.log"
    done

    CNT=0

    # Start workers as closely together as possible
    for h in $INITIATOR; do
	for t in `seq $THREADS_PER_INITIATOR`; do
	    ssh -q -tt simon@$h "ulimit -c unlimited; ./flextcp/tcptest_async.linux 128.208.6.130 1234" &

	    CNT=$((CNT + 1))

	    if [ $CNT -eq $NTHREADS ]; then
	    	break
	    fi
	done

	if [ $CNT -eq $NTHREADS ]; then
	    break
	fi
    done
}

# startflexkvslinux ()
# {
#     ssh -q -tt simon@$INITIATOR "ulimit -c unlimited; sudo ./flextcp/flexkvs/kvsbench -l 0-5 -n 2 -- 11211 80d00695 80d00680 a0369f10016c" &
# }

killall ()
{
    echo Killing all on $HOSTS $INITIATOR... Ignore errors after this.

    for h in $HOSTS $INITIATOR; do
	ssh simon@$h "sudo killall -9 -q flexnic tcpecho kernel tcpecho.linux tcptest_async tcptest_async.linux kvsbench flexkvs memcached dist_pagerank_flextcp dist_pagerank worker worker_mtcp || true" &
    done

    wait

    # ssh simon@$INITIATOR "killall -q socat || true"
    # ssh simon@$INITIATOR "sudo killall -q tcptest || true"
    # ssh simon@$INITIATOR "sudo killall -q tcptest_async tcptest_async.linux || true"
}

copylogs ()
{
    echo Copying logs from $INITIATOR to logs/ directory...
    rm -f logs/*.log
    for h in $INITIATOR; do
	rsync simon@$h:~/tcptest_*_*.log logs &
    done

    wait

    echo Post-processing logs...
    cat logs/*.log | sort -n -k2,2 > tcptest.log
}

case $1 in
    build)
	buildall all
	;;

    target)
	buildall $2
	;;

    bare)
	buildall all
	deployflexnic
	;;

    deploy)
	buildall all
	deployall
	;;

    linux)
	buildall testsuite
	deploylinux
	sleep 5

	echo "--- START --- (no output is OK)"
	startlinux
	sleep 20
	echo "--- STOP ---"

	killall
	copylogs
	;;

    flexkvs)
	buildall flexkvs-linux
	deployflexkvslinux
	sleep 3
	echo "--- Now start memc_benchmark ---"
	;;

    memcached)
	buildall memcached-linux
	deploymemcachedlinux
	sleep 3
	echo "--- Now start memc_benchmark ---"
	;;

    memcached-flextcp)
	buildall memcached-flextcp
	deployflexnic
	deploymemcachedflextcp
	sleep 5
	echo "--- Now start memc_benchmark ---"
	;;

    pagerank-flextcp)
	buildall pagerank-flextcp
	# deployflexnic
	# sleep 2
	deploypagerankflextcp
	;;

    pagerank-linux)
	buildall pagerank-flextcp
	deploypageranklinux
	;;

    deploy-flexnic)
	deployflexnic
	;;

    mystorm-linux)
	make -C mystorm deploy TOPOLOGY=SWINGOUT_BALANCED
	;;

    mystorm-flextcp)
	buildall all
	make -C mystorm deploy TOPOLOGY=SWINGOUT_FLEXTCP_BALANCED
	;;

    mystorm-mtcp)
	buildall all
	make -C mystorm deploy TOPOLOGY=SWINGOUT_MTCP_BALANCED
	;;

    start)
	startall
	;;

    kill)
	killall
	;;

    "")
	# No arg - do everything
	buildall all
	deployall
	sleep 3

	echo "--- START ---"
	startall
	sleep 10
	echo "--- STOP ---"

	killall
	copylogs
	;;
esac
