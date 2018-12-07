load "header.gnuplot"

set key bottom right

set ylabel "Average FCT [ms]" offset 2
set xlabel "Control interval ({/Symbol t}) [{/Symbol m}s]"
set yrange[0:1.550]
set xrange[0:]
set xtics ("0" 0, "200" 200, "400" 400, "600" 600, "800" 800, "1ms" 1000)
set ytics .5

plot \
    "fct-link.data" using 1:($2/1000) title "TCP" with linespoints ls 1 pt 2, \
    "fct-link.data" using 1:($3/1000) title "DCTCP" with linespoints ls 3 pt 1, \
    "fct-link.data" using 1:($4/1000) title "TAS" with linespoints ls 4 pt 4
