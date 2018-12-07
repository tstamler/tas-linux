all: storm_tput.eps storm_tput_split.eps

storm_tput.eps: storm_tput.gnuplot storm.dat mystorm_linux.pdat mystorm_mtcp.pdat mystorm_sockets.pdat mystorm_steering.pdat ../header.gnuplot
storm_tput_split.eps: storm_tput.gnuplot storm.dat mystorm_linux.pdat mystorm_mtcp.pdat mystorm_sockets.pdat mystorm_steering.pdat ../header.gnuplot

%.pdat: %.dat
	./percore.awk $< > $@

%.eps: %.gnuplot
	gnuplot $< > $@
	sed -e 's/setdash//' -i $@
