load "header.gnuplot"

set key bottom right

set ylabel "CDF" offset 2
set xlabel "Latency [ms]"
#set xrange[0:2000]

#set xtics (2000, 10000, 100000)
set logscale x

set yrange [0:1]

plot \
    "tcp-flows-long.data" using ($2/1000):1 title "TCP" with l ls 1, \
    "dctcp-flows-long.data" using ($2/1000):1 title "DCTCP" with l ls 3, \
    "flextcp-100-flows-long.data" using ($2/1000):1 title "TAS" with l ls 4
