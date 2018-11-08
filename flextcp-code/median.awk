#!/usr/bin/awk -f

# Median
NR == int(NLINES / 2) {
    median = $1;
}

# 99th percentile
NR == int((NLINES * 99) / 100) {
    p99 = $1;
}

# 99.9th percentile
NR == int((NLINES * 99.9) / 100) {
    p999 = $1;
}

# 99.99th percentile
NR == int((NLINES * 99.99) / 100) {
    p9999 = $1;
}

END {
    printf("median/99th/99.9th/99.99th percentile latency (us): %u, %u, %u, %u\n",
	   median, p99, p999, p9999);
}
