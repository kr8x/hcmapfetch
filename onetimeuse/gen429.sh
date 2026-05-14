#!/bin/bash
#
# gen429.sh - Generate 429 "Too Many Requests" graphics for all HamClock map sizes
#

mkdir -p 429

# Header filters to extract from .txt files
HEADER_FILTERS="Content-Type:|X-2Z-lengths:|Content-Length:"

# Outer loop: protocol versions
while IFS= read -r line; do
    v=$(echo "$line" | awk '{print $1}')
    prot=$(echo "$line" | awk '{print $2}')

    # Inner loop: HamClock map sizes
    while IFS= read -r size; do
        x=$(echo "$size" | awk '{print $1}')
        y=$(echo "$size" | awk '{print $2}')

        base="429-${prot}-${x}-${y}"

        echo "Generating $base (v=$v, x=$x, y=$y) ..."

        ./hcmapfetch.sh -s localhost -p 8042 -V "$v" -x "$x" -y "$y" \
            -l "$base" \
            -A "CAPTION=429\\n\\nToo_Many_Requests\\n\\nPlease_Try_Again_Later"

        # Copy matching header lines to 429/ folder
        grep -E "$HEADER_FILTERS" "${base}.txt" > "429/${base}.txt"

        # Copy binary output
        cp "${base}.bin" "429/${base}.bin"

        # Cleanup
        rm -f ./*.bin ./*.raw ./*.txt

        sleep 30

    done <<'SIZES'
660 330
1320    660
1980    990
2640    1320
3960    1980
5280    2640
5940    2970
7920    3960
SIZES

done <<'VERSIONS'
4.22 zlib
3.10 bmp
VERSIONS

echo "Done. Files are in 429/"