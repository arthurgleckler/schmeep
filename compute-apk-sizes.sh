#!/bin/bash

# Compute total size of each file type from APK listing
# Usage: unzip -l schmeep.apk | ./compute-apk-sizes.sh

awk '
BEGIN {
    in_data = 0
}

# Detect start of file listing (line with dashes)
/^---------/ {
    in_data = 1
    next
}

# Process file lines (length in field 1, filename in field 4)
in_data && /^[[:space:]]*[0-9]/ {
    size = $1
    filename = $4

    # Extract extension
    if (match(filename, /\.([^.\/]+)$/)) {
        ext = substr(filename, RSTART+1)
    } else if (filename ~ /\/$/) {
        ext = "directory"
    } else {
        ext = "no-extension"
    }

    # Accumulate totals
    total[ext] += size
    count[ext]++
}

END {
    # Sort by total size (descending)
    for (ext in total) {
        printf "%12d %6d  %s\n", total[ext], count[ext], ext
    }
}
' | sort -rn | awk '
BEGIN {
    printf "%-12s %6s  %s\n", "Total Bytes", "Count", "Extension"
    printf "%-12s %6s  %s\n", "------------", "------", "---------"
}
{
    printf "%-12s %6s  %s\n", $1, $2, $3
}
'
