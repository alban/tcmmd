#!/bin/sh

./tcmmd-log-parsing.py -i ${1} -o /tmp/tmp.$$
echo "set terminal png size 2048,768
set output '${1}.png'
set yrange [0:500000]
plot \
  '/tmp/tmp.$$' using 1:2 w linespoints title 'used bwidth total (stream+background)', \
  '/tmp/tmp.$$' using 1:3 w linespoints title 'used bwidth stream', \
  '/tmp/tmp.$$' using 1:4 w linespoints title 'used bwidth background', \
  '/tmp/tmp.$$' using 1:5 w linespoints title 'enforced bwidth background', \
  '/tmp/tmp.$$' using 1:6 w linespoints title 'gst percentage', \
  '/tmp/tmp.$$' using 1:7 w linespoints title 'link capacity' \
" | gnuplot

rm -f /tmp/tmp.$$
