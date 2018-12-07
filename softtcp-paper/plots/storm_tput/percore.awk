#!/usr/bin/awk -f

$1 == "#" {
    if($2 != "Sum") {
	cores = $2;
    }
}

$1 == "Raw" {
    avg = $4;
    min = $5;
    max = $6;
}

/^"Per core"/ {
    next;
}

{
    print;
}

END {
    printf("\"Per core\" 0.00 0.00 %.2f %.2f %.2f\n",
	   avg / cores, min / cores, max / cores);
}
