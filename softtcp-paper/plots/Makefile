all: \
  kvs/kvs.pdf \
  urpc/urpc.pdf \
  autoscale/autoscale.pdf \
  autoscale/autoscale_latency.pdf \
  unidir/unidir.pdf \
  storm_tput/storm_tput_split.pdf \
  ns3/fct-link.pdf ns3/fct-long-cluster.pdf ns3/fct-short-cluster.pdf ns3/queue-link.pdf \
  fairness_new/fairness.pdf \
  drops/throughput.pdf

%.pdf: %.eps
#       epstopdf --exact --outfile=$@.precrop $<
	sed -i 's/\/Author.*//' $<
	ps2pdf $< $@.precrop || true
	pdfcrop $@.precrop $@
	rm -f $@.precrop

%.eps: %.gnuplot
	(cd $(dir $<) && gnuplot $(notdir $<)) >$@
