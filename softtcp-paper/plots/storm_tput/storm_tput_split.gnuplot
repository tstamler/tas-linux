NORM = 1000000

load "../header.gnuplot"

set multiplot layout 1, 2 rowsfirst

set ylabel "Throughput [m tuples / s]"
set ytics 1
set yrange [0:]
set xrange [-.5:.5]
#set key at .3, 5.7 samplen 3 maxrows 1
set key at .3, 4.6 samplen 3 maxrows 1
set style data histograms
set style histogram errorbars gap 1

set bmargin 1.5
set tmargin 0.7
set lmargin 5.7
set rmargin 0

plot \
  'mystorm_linux.pdat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) every ::::0 title "Linux" fs solid .2, \
  'mystorm_mtcp.pdat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) every ::::0  title "mTCP" fs solid .4, \
  'mystorm_sockets.pdat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) every ::::0 notitle fs solid .6
#  'mystorm_steering.pdat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) every ::::0 notitle fs solid .8

unset key

set noylabel
set ytics .1
set yrange [0:.65]
set yrange [0:.7]
set key at .5, 0.8 width -7 maxrows 1

plot \
  'mystorm_linux.pdat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) every ::1::1 notitle fs solid .2, \
  'mystorm_mtcp.pdat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) every ::1::1 notitle fs solid .4, \
  'mystorm_sockets.pdat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) every ::1::1 title "TAS" fs solid .6
#  'mystorm_steering.pdat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) every ::1::1 title "  TAS/Steering" fs solid .8
