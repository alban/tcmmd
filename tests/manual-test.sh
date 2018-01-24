#!/bin/sh

if [ `id -u` != 0 ] ; then
  echo "Not root"
  exit 1
fi

if [ -z "$MAIN_LINK" ] ; then
  echo "Environment variable \$MAIN_LINK not set. Please set it to eth0, ens32, or wlan0."
  exit 1
fi
if [ -z "$PORT" ] ; then
  PORT=12345
fi

echo "Deleting traffic control rules on ${MAIN_LINK}..."
tc qdisc del dev $MAIN_LINK ingress > /dev/null 2>&1
echo "Removing ifb module..."
rmmod ifb > /dev/null 2>&1

echo "Adding traffic control..."
modprobe ifb

ifconfig ifb0 up

tc qdisc add dev $MAIN_LINK handle ffff: ingress
tc filter add dev $MAIN_LINK parent ffff: protocol ip u32 match u32 0 0 action mirred egress redirect dev ifb0

tc qdisc add dev ifb0 handle 1:0 root dsmark indices 4 default_index 0
tc qdisc add dev ifb0 handle 2:0 parent 1:0 htb r2q 2
tc class add dev ifb0 parent 2:0 classid 2:1 htb rate 50000bps ceil 50000bps # ssh
tc qdisc add dev ifb0 handle 3:0 parent 2:1 sfq
tc class add dev ifb0 parent 2:0 classid 2:2 htb rate 80000bps # stream
tc qdisc add dev ifb0 handle 4:0 parent 2:2 sfq
tc class add dev ifb0 parent 2:0 classid 2:3 htb rate 5000bps ceil 5000bps # background
tc qdisc add dev ifb0 handle 5:0 parent 2:3 sfq
tc filter add dev ifb0 parent 2:0 protocol all prio 1 tcindex mask 0x3 shift 0
tc filter add dev ifb0 parent 2:0 protocol all prio 1 handle 3 tcindex classid 2:3
tc filter add dev ifb0 parent 2:0 protocol all prio 1 handle 2 tcindex classid 2:2
tc filter add dev ifb0 parent 2:0 protocol all prio 1 handle 1 tcindex classid 2:1

tc filter add dev ifb0 parent 1:0 protocol all prio 1 handle 1:0:0 u32 divisor 1
tc filter add dev ifb0 parent 1:0 protocol all prio 1 u32 match u8 0x6 0xff at 9 offset at 0 mask 0f00 shift 6 eat link 1:0:0
tc filter add dev ifb0 parent 1:0 protocol all prio 1 handle 1:0:1 u32 ht 1:0:0 match u16 0x16 0xffff at 2 classid 1:1
tc filter add dev ifb0 parent 1:0 protocol all prio 1 handle 2:0:0 u32 divisor 1
tc filter add dev ifb0 parent 1:0 protocol all prio 1 u32 match u8 0x6 0xff at 9 offset at 0 mask 0f00 shift 6 eat link 2:0:0
tc filter add dev ifb0 parent 1:0 protocol all prio 1 handle 2:0:1 u32 ht 2:0:0 match u16 $PORT 0xffff at 2 classid 1:2
tc filter add dev ifb0 parent 1:0 protocol all prio 1 u32 match u32 0x0 0x0 at 0 classid 1:3
echo "Traffic control rules added. You can test it now."

echo "Press <enter> to remove the traffic control rules."
read i

tc qdisc del dev ifb0 root
tc qdisc del dev $MAIN_LINK ingress
echo "Traffic control rules removed."


