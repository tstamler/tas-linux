load "../header.gnuplot"
set ylabel "T'put [mB / 100 ms]"
set xlabel "# of Connections"
set xrange [40:2700]
set yrange [0.01:3]
#set key top right
set logscale yx

set xtics (50, 100, 200, 500, 1000, 2000)

# columns:
# stack flows jfi mean min .1% 1% 10% 25% 50% 75% 90% 99% 99.9% max
# 1     2     3   4    5   6   7  8   9   10  11  12  13  14    15


plot \
  'results_linux.dat' using 2:($10/1024/1024) with linespoints title 'Linux Median' pt 1 lw 3, \
  'results_ftcp.dat' using 2:($10/1024/1024) with linespoints title 'TAS Median' pt 2, \
  'results_ftcp.dat' using 2:($7/1024/1024) with linespoints title 'TAS 99th %' pt 3, \
  'results_ftcp.dat' using 2:(100000000/$2/1024/1024) with linespoints title 'Fair Share' pt -1 lw 2,

#  'results_linux.dat' using 2:($8/1024/1024) with linespoints title 'Linux 99th %', \
