#!/bin/bash

error_msg() {
  (>&2 echo "Error: $*")
}

die() {
  error_msg "$*"
  exit 1
}


machine_if_info() {
  h=`ip addr | awk '/inet 19/{print substr($2,0,14)}'`
  case $h in
    swingout1*)
      if_pci=0000:08:00.0
      if_driver=ixgbe
      if_ip=10.0.0.1
      ;;
    192.168.1.102)
      if_pci=0000:02:00.0
      if_driver=ixgbe
      if_ip=192.168.1.102
      ;;
    192.168.1.110)
      if_pci=0000:01:00.1
      if_driver=ixgbe
      if_ip=192.168.1.110
      ;;
    pune-lom)
      if_pci=0000:03:00.1
      if_driver=tg3
      if_ip=192.168.1.122
      ;;
    swingout2*)
      if_pci=0000:09:00.1
      if_driver=ixgbe
      if_ip=10.0.0.2
      ;;

    swingout3*)
      if_pci=0000:0c:00.1
      if_driver=ixgbe
      if_ip=10.0.0.3
      ;;

    swingout4*)
      if_pci=0000:09:00.0
      if_driver=ixgbe
      if_ip=10.0.0.4
      ;;

    swingout5*)
      if_pci=0000:0c:00.0
      if_driver=ixgbe
      if_ip=10.0.0.5
      ;;

    bigfish*)
      if_pci=0000:09:00.1
      if_driver=ixgbe
      if_ip=10.0.0.7
      ;;

    rhinoceros*)
      if_pci=0000:02:00.1
      if_driver=i40e
      if_ip=10.0.0.10
      ;;

    sloth*)
      if_pci=0000:02:00.1
      if_driver=i40e
      if_ip=10.0.0.11
      ;;

    fnhost*)
      if_pci=0000:b4:00.0
      if_driver=i40e
      if_ip=10.0.0.12
      dpdk_hugepages=1024
      ;;

    *)
      error_msg "Unknown host: `hostname`"
      return -1
  esac

  if [ "$ROUTED" = "1" ]; then
    case $h in
      swingout1*)
        ip_pref=10.0.1
        ;;
      swingout2*)
        ip_pref=10.0.2
        ;;
      swingout3*)
        ip_pref=10.0.3
        ;;
      swingout4*)
        ip_pref=10.0.4
        ;;
      swingout5*)
        ip_pref=10.0.5
        ;;
      bigfish*)
        ip_pref=10.0.7
        ;;
      rhinoceros*)
        ip_pref=10.0.10
        ;;
      sloth*)
        ip_pref=10.0.11
        ;;
      fnhost*)
        ip_pref=10.0.12
        ;;
    esac
    if_ip=${ip_pref}.1
    if_gw=${ip_pref}.254
    if_net=${ip_pref}.0/24
  fi

  if [ "$IP_OVERRIDE" != "" ]; then
    if_ip="$IP_OVERRIDE"
  fi
}

pci_to_ifname() {
  if [[ -d /sys/bus/pci/devices/$1/net ]]; then
    local if_name=$(ls -1 /sys/bus/pci/devices/$1/net | head)
    if [[ ! -z $if_name ]]; then
      echo $if_name
    else
      return 1
    fi
  else
    return 1
  fi
}

dpdk_prepare() {
  if [[ -z $dpdk_path ]]; then
    error_msg "dpdk_path not set"
    return -1
  fi
  if [[ -z $dpdk_hugepages ]]; then
    dpdk_hugepages=1024
    # dpdk_hugepages=96
  fi

  # load uio kernel module
  sudo modprobe uio
  sudo rmmod igb_uio.ko >/dev/null 2>/dev/null
  sudo insmod $dpdk_path/build/kmod/igb_uio.ko || \
    die "insmod failed"

  # mount huge page fs
  sudo mkdir -p /mnt/huge
  grep -s '/mnt/huge' /proc/mounts > /dev/null
  if [ $? -ne 0 ] ; then
    sudo mount -t hugetlbfs nodev /mnt/huge || die "mounting hugetlbfs failed"
  fi
  sudo rm -f /mnt/huge/*


  # allocate huge pages
  for d in /sys/devices/system/node/node? ; do
    echo $dpdk_hugepages | \
      sudo tee $d/hugepages/hugepages-2048kB/nr_hugepages >/dev/null ||\
      die "allocating huge pages failed"
  done

  echo "Didn't Die after allocating hugepages"
  sudo rm -rf /dev/dpdk-iface
  sudo mknod /dev/dpdk-iface c 1110 0
  sudo chmod 666 /dev/dpdk-iface
  echo "Didn't Die end"
}

dpdk_cleanup() {
  sudo rmmod igb_uio >/dev/null 2>/dev/null
  grep -s '/mnt/huge' /proc/mounts > /dev/null
  if [ $? -eq 0 ] ; then
    sudo umount /mnt/huge || error_msg "Unmounting /mnt/huge failed"
  fi
  for d in /sys/devices/system/node/node? ; do
    echo 0 | \
      sudo tee $d/hugepages/hugepages-2048kB/nr_hugepages >/dev/null ||\
      die "allocating huge pages failed"
  done
}

dpdk_bind_nic() {
 
  echo "Didn't die at bind_nic start"	
  local if_name
  if [[ -z $if_pci ]]; then
    error_msg "if_pci not set"
    return -1
  fi
  if [[ -z $if_ip ]]; then
    error_msg "if_ip not set"
    return -1
  fi

  echo "bind nic after setting pci ip"
  if_name=$(pci_to_ifname $if_pci)
  if [ $? -eq 0 ] ; then
    echo "if_name = "$if_name
    sudo ifconfig $if_name 0.0.0.0 down || die "ifconfig down failed"
  fi

  echo "bind nic before finding dpdk_nic_bind"

  if [ -x $dpdk_path/usertools/dpdk_nic_bind.py ] ; then
    bind_tool="$dpdk_path/usertools/dpdk_nic_bind.py"
  elif [ -x $dpdk_path/usertools/dpdk-devbind.py ] ; then
    bind_tool="$dpdk_path/usertools/dpdk-devbind.py"
  else
    die "dpdk bind tool not found"
  fi

  echo "bind_tool = "$bind_tool
  echo "dpdk_path = "$dpdk_path

  sudo $bind_tool -b igb_uio $if_pci >/dev/null || \
    die "binding nic failed"
}

if_config_ip() {
  local if_name=$(pci_to_ifname $if_pci)
  if [ $? -ne 0 ] ; then
    return -1
  fi

  sudo ifconfig $if_name $if_ip netmask 255.255.255.0 up || \
    die "ifconfig failed"
  if [ "$ROUTED" = "1" ]; then
    for i in `seq 1 11` ; do
      pref=10.0.$i
      if [ "$pref" = "$ip_pref" ]; then
        continue
      fi
      sudo route add -net ${pref}.0/24 gw $if_gw
    done
  fi

  for r in $ROUTES ; do
    sudo route add -net `echo $r | sed 's/,/ gw /'`
  done
}

dpdk_unbind_nic() {
  if [[ -z $if_pci ]]; then
    error_msg "if_pci not set"
    return -1
  fi
  if [[ -z $if_driver ]]; then
    error_msg "if_driver not set"
    return -1
  fi
  if [[ -z $if_ip ]]; then
    error_msg "if_ip not set"
    return -1
  fi
  if [[ -z $dpdk_path ]]; then
    error_msg "dpdk_path not set"
    return -1
  fi

  local if_name
  if_name=$(pci_to_ifname $if_pci)
  if [ $? -eq 0 ] ; then
    sudo ifconfig $if_name 0.0.0.0 down || die "ifconfig down failed"
  fi

  if [[ -x $dpdk_path/tools/dpdk_nic_bind.py ]] ; then
    bind_tool="$dpdk_path/tools/dpdk_nic_bind.py"
  elif [[ -x $dpdk_path/tools/dpdk-devbind.py ]] ; then
    bind_tool="$dpdk_path/tools/dpdk-devbind.py"
  else
    die "dpdk bind tool not found"
  fi

  sudo $bind_tool -b $if_driver $if_pci >/dev/null || \
    die "unbinding nic failed"

  if_config_ip
}

clear_fn_shm() {
  sudo rm -rf /dev/shm/flexnic*
}
