load "../header.gnuplot"
set ylabel "Throughput [mOps]"
set xlabel "Cores"
set xrange [1:17]
set yrange [0:]
set ytics 2
set xtics (1, 2, 4, 6, 8, 10, 12, 14, 16)
#set key at 16,13 spacing 1.28 width -1.5 maxrows 2
set key top left spacing 1.28 width -1.5 maxrows 3

plot \
  'flexkvs_tas.dat' using 2:7 title 'TAS' with linespoints pt 2, \
  'flexkvs_ix.dat' using 1:2 title 'IX' with linespoints pt 4, \
  'flexkvs_mtcp.dat' using 2:5 title 'mTCP' with linespoints pt 1, \
  'flexkvs_linux.dat' using 2:5 title 'Linux' with linespoints pt 3, \
