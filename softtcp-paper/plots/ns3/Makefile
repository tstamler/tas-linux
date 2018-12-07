all: fct-link.eps fct-long-cluster.eps fct-short-cluster.eps queue-link.eps

%.eps: %.gnuplot
	gnuplot $< > $@
#	sed -e 's/setdash//' -i $@
