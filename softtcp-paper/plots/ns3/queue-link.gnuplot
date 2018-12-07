load "header.gnuplot"

set key bottom right

set ylabel "Queue size [pkts]" offset 2
set xlabel "Control interval ({/Symbol t}) [{/Symbol m}s]"
set yrange[5:150]
set xrange[0:]
set xtics ("0" 0, "200" 200, "400" 400, "600" 600, "800" 800, "1ms" 1000)
set log y 10

plot \
    "queue-link.data" using 1:($2/500) title "TCP" with linespoints ls 1 pt 2, \
    "queue-link.data" using 1:($3/500) title "DCTCP" with linespoints ls 3 pt 1, \
    "queue-link.data" using 1:($4/500) title "TAS" with linespoints ls 4 pt 4
