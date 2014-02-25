#!/bin/bash

# limit egress bandwidth with source tcp port 80

set -x

# 375 MB/s
rate=3Mbit

tc qdisc del dev wlan0 root

tc qdisc add dev wlan0 root handle 1: cbq avpkt 1000 bandwidth 10Mbit
tc class add dev wlan0 parent 1: classid 1:1 cbq rate $rate allot 1500 prio 3 bounded isolated
tc filter add dev wlan0 parent 1: protocol ip u32 match ip protocol 6 0xff match ip sport 80 0xffff flowid 1:1
