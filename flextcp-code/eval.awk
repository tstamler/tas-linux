#!/usr/bin/awk -f

BEGIN {
    one3rd = NLINES / 3;
    two3rd = one3rd * 2;
    minlatency = 999999;
    THRESHOLD = 1000000;	# Filter latencies larger than THRESHOLD
}

/[0-9]+ *[0-9]+/ && NR >= one3rd && NR < two3rd {
    if($2 - $1 < THRESHOLD) {
	avglatency += $2 - $1;
	minlatency = minlatency < $2 - $1 ? minlatency : $2 - $1;
	maxlatency = maxlatency > $2 - $1 ? maxlatency : $2 - $1;
    }
    # print $2, $2 - $1;
    tput++;
    if(lasttime == 0) {
	lasttime = $2;
    }
    if($2 > lasttime + 1000000) {
	printf("Throughput: %u reqs/s\n", tput);
	tput = 0;
	lasttime = $2;
    }
    num++;

    if($2 - $1 < THRESHOLD) {
	printf("%u\n", $2 - $1) > "/dev/stderr";
    }
}

END {
    printf("min/avg/max latency (us): %u, %.2f, %u\n", minlatency, avglatency / num, maxlatency);
}
