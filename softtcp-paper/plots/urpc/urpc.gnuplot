load "../header_large.gnuplot"

set label 2 'Latency [us]       Throughput [mOp/s]' at screen -.1,0.15 rotate by 90

set multiplot layout 2, 1
set bmargin 1.5
set tmargin 0
set lmargin 0
set rmargin 0

set logscale x 2
set xrange [4:2048]
set xtics

#set key at 9,2.7
set key top left
set yrange [0:6]
set ytics 1.0
set xtics ("" 1 1, "" 2 0 , "" 4 1, "" 8 0, "" 16 1, "" 32 0, "" 64 1, "" 128 0, "" 256 1, "" 512 0, "" 1024 1, "" 2048 0)
set format y " %.1f"
plot \
  'result_tas.dat' using 2:3:4:5 title 'TAS' with yerrorlines \
    pt 1, \
  'result_ix.dat' using 2:3:3:3 title 'IX' with yerrorlines \
    pt 2, \
  'result_mtcp.dat' using 2:3:4:5 title 'mTCP' with yerrorlines \
    pt 4, \
  'result_linux.dat' using 2:3:4:5 title 'Linux' with yerrorlines \
    pt 3

unset key
set format y "%.0f"
set yrange [10:5000]
set ytics ("10" 10, "100" 100, "1ms" 1000, "5ms" 5000)
set logscale y 10
set xlabel "# of Connections [log scale]"
set xtics ("" 1 1, "2" 2 0 , "" 4 1, "8" 8 0, "" 16 1, "32" 32 0, "" 64 1, "128" 128 0, "" 256 1, "512" 512 0, "" 1024 1, "2048" 2048 0)
plot \
  'result_tas.dat' using 2:6:6:9 title 'TAS' with yerrorlines \
    pt 1, \
  'result_ix.dat' using 2:4:4:5 title 'IX' with yerrorlines \
    pt 2, \
  'result_mtcp.dat' using 2:6:6:9 title 'mTCP' with yerrorlines \
    pt 4, \
  'result_linux.dat' using 2:6:6:9 title 'Linux' with yerrorlines \
    pt 3
