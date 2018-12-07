load "../header.gnuplot"
set ylabel "Throughput penalty [%]"
set xlabel "Packet drop rate [%]"
set logscale xy
#set xrange [0.1:6]
set yrange [40:0.09] reverse
set xtics (.1, .2, .5, 1, 2, 5)
set key left bottom Left reverse
set boxwidth 0.25

plot \
  'results_linux.dat' using ($2*100):((1-$3/1176612477)*100) every ::1 \
    title 'Linux' with linespoints pt 1, \
  'results_fn_new.dat' using ($2*100):((1-$3/1176916273)*100) every ::1 \
    title 'TAS' with linespoints pt 2, \
  'results_fn.dat' using ($2*100):((1-$3/1176914329)*100) every ::1 \
    title 'TAS simple recovery' with linespoints pt 3
