#!/usr/bin/python

import sys
import argparse

parser = argparse.ArgumentParser(description='Parsing tcmmd logs to feed gnuplot')
parser.add_argument('-i','--input', help='input file',required=True)
parser.add_argument('-o','--output', help='output file', required=True)
args = parser.parse_args()

outf = open(args.output, 'w')

link_capacity=375000

with open(args.input) as f:
    t1, t2, t3, t4, t5, t6 = [x for x in f.readline().split()] # read first line
    previous_l1, previous_l2, previous_l3, previous_l4, previous_l5, previous_l6 = [0, 0, 0, 0, 0, 0]
    time_origin = 0.0
    for line in f: # read rest of lines
        l1, l2, l3, l4, l5, l6 = [x for x in line.split()]
        l1 = float(l1)
        if (time_origin == 0.0):
          time_origin = l1
        l1 = l1 - time_origin
        l2 = max(int(l2) - int(previous_l2), 0)
        l3 = max(int(l3) - int(previous_l3), 0)
        l4 = max(int(l4) - int(previous_l4), 0)
        l5 = int(l5)
        l6 = int(l6) * link_capacity / 100 # just so it looks ok on the graph
        outf.write(str(l1) + " " + str(l2) + " " + str(l3) + " " + str(l4) + " " + str(l5) + " " + str(l6) + " " + str(link_capacity) + "\n")
        previous_l1, previous_l2, previous_l3, previous_l4, previous_l5, previous_l6 = line.split()

