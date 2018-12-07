NORM = 1000000

load "../header.gnuplot"
set ylabel "Throughput [m tuples / s]"
set ytics 1
set yrange [0:]
set xrange [-.6:1.6]
#unset xtics
#set key at 0.2,5.9
set key top right
#set boxwidth -2.0
#set boxwidth 1.5 relative
set style data histograms
set style histogram errorbars gap 1
#set style fill solid 1.0 border lt -1

plot \
  'mystorm_linux.pdat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) title "Linux" fs solid .2, \
  'mystorm_mtcp.pdat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) title "mTCP" fs solid .4, \
  'mystorm_sockets.pdat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) title "TAS/Sockets" fs solid .6, \
  'mystorm_steering.pdat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) title "TAS/Steering" fs solid .8

# 'storm.dat' using ($4 / NORM):($5 / NORM):($6 / NORM):xticlabels(1) title "Apache Storm" fs solid 1, \
