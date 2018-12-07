load "../header.gnuplot"
set ylabel "Cores"
set y2label "Latency [us]"
set xlabel "Time [s]"
#set xrange [1:17]
set yrange [0:]
set y2range [0:70]
set xrange [35:49]
set ytics nomirror 2
set y2tics
#set xtics (1, 2, 4, 6, 8, 10, 12, 14, 16)
#set key at 16,13 spacing 1.28 width -1.5 maxrows 2
#set key top left spacing 1.28 width -1.5 maxrows 3
set key bottom center horizontal

plot \
  'stagger.dat' using 1:2 axis x1y1 title 'Cores' with lines, \
  'stagger.dat' using 1:6 axis x1y2 title 'Latency' with linespoints
