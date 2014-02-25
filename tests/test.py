#!/usr/bin/env python

import sys
import argparse
import dbus

parser = argparse.ArgumentParser(description='Testing tcmmd')
parser.add_argument('-S','--ipsrc', help='IP source',required=True)
parser.add_argument('-D','--ipdst', help='IP destination', required=True)
parser.add_argument('-s','--tcpsport', help='TCP source port',required=True)
parser.add_argument('-d','--tcpdport', required=True,
                    help='TCP destination port')
parser.add_argument('-B','--backgroundrate', required=True,
                    help='Background rate in B/s')
parser.add_argument('-P','--priorityrate', required=True,
                    help='Priority stream rate in B/s')
args = parser.parse_args()

bus = dbus.SystemBus()
remote_object = bus.get_object("org.tcmmd",
                               "/org/tcmmd/ManagedConnections")
iface = dbus.Interface(remote_object, "org.tcmmd.ManagedConnections")

print("{0}:{1} -> {2}:{3}  -  stream: {4} B/s  background: {5} B/s".format(
        str(args.ipsrc), int(args.tcpsport),
        str(args.ipdst), int(args.tcpdport),
        int(args.priorityrate), int(args.backgroundrate)))

ret = iface.SetFixedPolicy(str(args.ipsrc), int(args.tcpsport),
                           str(args.ipdst), int(args.tcpdport),
                           int(args.priorityrate),
                           int(args.backgroundrate))

print("Traffic control rules added.")
out = raw_input("Press Enter to continue...")

