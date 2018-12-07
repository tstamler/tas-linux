set macros
load "../header_large.gnuplot"

set multiplot layout 2, 2 rowsfirst


set logscale x 2
set logscale y 10
set xrange [25:2560]
set yrange [:80000]
set xtics rotate
set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set bmargin 0
set tmargin 0
set lmargin 0
set rmargin 0

set ytics ("" 1, "10M" 10, ".1G" 100, "1G" 1000, "10G" 10000, "40G" 40000)
set xtics ("" 1 1, "" 2 0 , "" 4 1, "" 8 0, "" 16 1, "" 32 0, "" 64 1, "" 128 0, "" 256 1, "" 512 0, "" 1024 1, "" 2048 0)
set ylabel "RX Throughput"
set x2label "250 Cycles/Message"
unset key

plot \
  'results_rx_fn_so_250.dat' using 4:($5*8/1000000) title 'TAS' with linespoints \
    pt 1, \
  'results_rx_linux_250.dat' using 4:($5*8/1000000) title 'Linux' with linespoints \
    pt 3

unset ylabel
set ytics ("" 1, "" 10, "" 100, "" 1000, "" 10000, "" 40000)
set x2label "1000 Cycles/Message"

plot \
  'results_rx_fn_so_1000.dat' using 4:($5*8/1000000) title 'TAS' with linespoints \
    pt 1, \
  'results_rx_linux_1000.dat' using 4:($5*8/1000000) title 'Linux' with linespoints \
    pt 3

unset x2label
set xlabel "Message Size [bytes]"
set ytics ("" 1, "10M" 10, ".1G" 100, "1G" 1000, "10G" 10000, "40G" 40000)
set xtics ("" 1 1, "2" 2 0 , "" 4 1, "8" 8 0, "" 16 1, "32" 32 0, "" 64 1, "128" 128 0, "" 256 1, "512" 512 0, "" 1024 1, "2048" 2048 0)
set ylabel "TX Throughput"
set key right bottom

plot \
  'results_tx_fn_so_250.dat' using 4:($5*8/1000000) title 'TAS' with linespoints \
    pt 1, \
  'results_tx_mtcp_250.dat' using 4:($5*8/1000000) notitle with linespoints \
    pt 4, \
  'results_tx_linux_250.dat' using 4:($5*8/1000000) notitle with linespoints \
    pt 3

unset ylabel
set ytics ("" 1, "" 10, "" 100, "" 1000, "" 10000, "" 40000)

plot \
  'results_tx_fn_so_1000.dat' using 4:($5*8/1000000) notitle with linespoints \
    pt 1, \
  'results_tx_mtcp_1000.dat' using 4:($5*8/1000000) title 'mTCP' with linespoints \
    pt 4, \
  'results_tx_linux_1000.dat' using 4:($5*8/1000000) title 'Linux' with linespoints \
    pt 3

