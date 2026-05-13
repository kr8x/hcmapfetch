#!/usr/bin/env bash
# hcmapfetch.sh — fetch and demux HamClock map files
#
# Usage:
#   hcmapfetch.sh [options]
#
# Options:
#   -s HOST       HamClock server host or IP  (default: hamclock.local)
#   -p PORT       HamClock server port        (default: 8080)
#   -u PATH       URL path for the map        (default: /get_maps.bin)
#   -V VERSION    HamClock version to emulate: 4 (default) or 3
#   -x WIDTH      Map pixel width  (required for version 3)
#   -y HEIGHT     Map pixel height (required for version 3)
#   -d FILE       Day   map output filename   (default: day_map.bmp)
#   -n FILE       Night map output filename   (default: night_map.bmp)
#   -b BINARY     Path to hcmapdemux binary   (default: ./hcmapdemux)
#   -A ARG        Add and argument to the Curl  X=Y becomes &X=Y 
#   --help        Show this help

set -euo pipefail

# ------------------------------------------------------------------ #
#  Defaults                                                           #
# ------------------------------------------------------------------ #
HC_HOST="clearskyinstitute.com"
HC_PORT="80"

HC_VERSION="4.22"
MAP_WIDTH="660"
MAP_HEIGHT=""
DAY_OUT="day_map.bmp"
NIGHT_OUT="night_map.bmp"
DEMUX_BIN="./hcmapdemux"
# ------------------------------------------------------------------ #
#  Date/time — current UTC                                            #
# ------------------------------------------------------------------ #
YEAR=$(date -u +%Y)
MONTH=$(date -u +%-m)          # no leading zero; on macOS: $(( 10#$(date -u +%m) ))
HOUR=$(date -u +%-H)           # no leading zero

# ------------------------------------------------------------------ #
#  Parameters (override with script args)                             #
# ------------------------------------------------------------------ #
MODE="19"            # 19
TXLAT="33.167"
TXLNG="-96.917"
WATTS="100"
MAP_WIDTH="660"  # also passed to hcmapdemux -x
MAP_HEIGHT="330" # also passed to hcmapdemux -y
MHZ="3.60"
TOA="3.0"
REQ="REL"
ARG=""
case "$REQ" in
    REL) VOACAPTYPE="Area" ;;
    TOA) VOACAPTYPE="-TOA" ;;
    MUF) VOACAPTYPE="-MUF" ;;
esac

# ------------------------------------------------------------------ #
#  Build default URL for --help option                               #
# ------------------------------------------------------------------ #
HC_PATH=$(printf \
    "/ham/HamClock/fetchVOACAP%s.pl?YEAR=%d&MONTH=%d&UTC=%d&TXLAT=%s&TXLNG=%s&PATH=0&WATTS=%s&WIDTH=%s&HEIGHT=%s&MHZ=%s&TOA=%s&MODE=%s" \
    "$VOACAPTYPE" "$YEAR" "$MONTH" "$HOUR" "$TXLAT" "$TXLNG" "$WATTS" \
    "$MAP_WIDTH" "$MAP_HEIGHT" "$MHZ" "$TOA" "$MODE")


# ------------------------------------------------------------------ #
#  Usage                                                             #
# ------------------------------------------------------------------ #
usage() {
    cat <<EOF

Usage: $(basename "$0") [options]

Fetch a HamClock map response and split it into day and night BMP files.

Options:
  -s HOST       HamClock server host or IP  (default: $HC_HOST)
  -p PORT       HamClock server port        (default: $HC_PORT)
  -u PATH       URL path for the map        (default: $HC_PATH)
  -V VERSION    HamClock version: 4.22 (zlib, default) or 3.10 (raw BMP)
  -x WIDTH      Map pixel width  (required for -V 3) see valid x/y combinations
  -y HEIGHT     Map pixel height (required for -V 3) see valid x/y combinations
      660/330 1320/660 1980/990 2640/1320 3960/1980 5280/2640 5940/2970 7920x3960
  -d FILE       Day   map output            (default: $DAY_OUT)
  -n FILE       Night map output            (default: $NIGHT_OUT)
  -b BINARY     Path to hcmapdemux          (default: $DEMUX_BIN)
  -m MODE       Mode for area map  (default: $MODE)
                CW 19 RTTY 22 SSB 38 AM 49 SWPR 3 FT8 13 FT4 17
  -t TXLAT      TX latitude (default: $TXLAT)
  -T TXLNG      TX longitude (default: $TXLNG)
  -w WATTS      WATTS (default: $WATTS)
  -f MHZ        MHz (default: $MHZ)
          80M  3.6 40M  7.1 30M 10.1 20M 14.1 17M 18.1 15M 21.1 12M 24.9 10M 28.2
  -o TOA        Take Off Angle (default: $TOA)
  -r REQ        Map Request Type REL,TOA,MUF (defualt: REL)
  -A ARG        Add argument to query if ARG is X=Y, Query is added &X=Y
  -l CURL       save curl file and split into header CURL.txt body CURL.bin 
  --help        Show this help

Examples:
  # v4.22+ zlib — no dimensions needed
  $(basename "$0") -s clearskyinstitute.com -V 4.22

  # v3.10 raw BMP
  $(basename "$0") -s clearskyinstitute.com -V 3.10 -w 800 -h 400

  # Custom output names
  $(basename "$0") -s clearskyinstitute.com -V 4.25 -d /tmp/day.bmp -n /tmp/night.bmp

EOF
    exit 1
}

# ------------------------------------------------------------------ #
#  Parse arguments                                                   #
# ------------------------------------------------------------------ #
while [[ $# -gt 0 ]]; do
    case "$1" in
        -s) HC_HOST="$2";   shift 2 ;;
        -p) HC_PORT="$2";   shift 2 ;;
        -u) HC_PATH="$2";   shift 2 ;;
        -V) HC_VERSION="$2"; shift 2 ;;
        -x) MAP_WIDTH="$2"; shift 2 ;;
        -y) MAP_HEIGHT="$2"; shift 2 ;;
        -d) DAY_OUT="$2";   shift 2 ;;
        -n) NIGHT_OUT="$2"; shift 2 ;;
        -b) DEMUX_BIN="$2"; shift 2 ;;
		-m) MODE="$2"; shift 2 ;;
		-t) TXLAT="$2"; shift 2 ;;
		-T) TXLNG="$2"; shift 2 ;;
		-w) WATTS="$2"; shift 2 ;;
		-f) MHZ="$2"; shift 2 ;;
		-o) TOA="$2"; shift 2 ;;
		-r) REQ="$2"; shift 2 ;;
		-A) ARG="$2"; shift 2 ;;
		-l) CURL="$2"; shift 2 ;;

        --help) usage ;;
        *) echo "Unknown option: $1"; usage ;;
    esac
done

# ------------------------------------------------------------------ #
#  Validate                                                           #
# ------------------------------------------------------------------ #
if [[ ! -x "$DEMUX_BIN" ]]; then
    echo "error: hcmapdemux binary not found or not executable: $DEMUX_BIN"
    echo "       Build with: make"
    exit 1
fi

if [[ "$HC_VERSION" == "3.10" ]]; then
    if [[ -z "$MAP_WIDTH" || -z "$MAP_HEIGHT" ]]; then
        echo "error: -x WIDTH and -y HEIGHT are required for version 3"
        usage
    fi
fi



# ------------------------------------------------------------------ #
#  Parameters (override with script args)                             #
# ------------------------------------------------------------------ #
MODE="${MODE:-REL}"            # MUF | REL | TOA
TXLAT="${TXLAT:-33.167}"
TXLNG="${TXLNG:--96.917}"
WATTS="${WATTS:-100}"
MAP_WIDTH="${MAP_WIDTH:-660}"  # also passed to hcmapdemux -x
MAP_HEIGHT="${MAP_HEIGHT:-330}" # also passed to hcmapdemux -y
MHZ="${MHZ:-3.60}"
TOA="${TOA:-9.0}"

# ------------------------------------------------------------------ #
#  Validate mode                                                      #
# ------------------------------------------------------------------ #
case "$MODE" in
    19|22|38|49|3|13|17) ;;
    *) echo "error: -m MODE must be 19, REL, or TOA (got: $MODE)"; usage ;;
esac

# ------------------------------------------------------------------ #
#  Build URL                                                          #
# ------------------------------------------------------------------ #
case "$REQ" in
    REL) VOACAPTYPE="Area" ;;
    TOA) VOACAPTYPE="-TOA" ;;
    MUF) VOACAPTYPE="-MUF" ;;
esac

#add parameter separator to $ARG if it has a value
[ -n "$ARG" ] && ARG="&$ARG"

# ------------------------------------------------------------------ #
#  Build default URL for --help option                               #
# ------------------------------------------------------------------ #
HC_PATH=$(printf \
    "/ham/HamClock/fetchVOACAP%s.pl?YEAR=%d&MONTH=%d&UTC=%d&TXLAT=%s&TXLNG=%s&PATH=0&WATTS=%s&WIDTH=%s&HEIGHT=%s&MHZ=%s&TOA=%s&MODE=%s%s" \
    "$VOACAPTYPE" "$YEAR" "$MONTH" "$HOUR" "$TXLAT" "$TXLNG" "$WATTS" \
    "$MAP_WIDTH" "$MAP_HEIGHT" "$MHZ" "$TOA" "$MODE" "$ARG")


# ------------------------------------------------------------------ #
#  Build curl and demux arguments                                     #
# ------------------------------------------------------------------ #
URL="http://${HC_HOST}:${HC_PORT}${HC_PATH}"

CURL_ARGS=(
    --silent
    --show-error
    --include          # prepend HTTP headers to output (-i)
    --fail-with-body   # exit non-zero on HTTP 4xx/5xx but still show body
)

CURL_ARGS+=(--user-agent "HamClock-linux/$HC_VERSION")

DEMUX_ARGS=(
    -d "$DAY_OUT"
    -n "$NIGHT_OUT"
)

if [[ -n "$MAP_WIDTH" ]];  then DEMUX_ARGS+=(-x "$MAP_WIDTH");  fi
if [[ -n "$MAP_HEIGHT" ]]; then DEMUX_ARGS+=(-y "$MAP_HEIGHT"); fi

# ------------------------------------------------------------------ #
#  Run                                                                #
# ------------------------------------------------------------------ #
echo "hcmapfetch: fetching $URL  (protocol v${HC_VERSION})"
echo "hcmapfetch: day  -> $DAY_OUT"
echo "hcmapfetch: night-> $NIGHT_OUT"
echo ""
TEMP=$(mktemp /tmp/hcmapfetch.XXXXXX)
curl "${CURL_ARGS[@]}" "$URL" | tee $TEMP | "$DEMUX_BIN" "${DEMUX_ARGS[@]}"

echo ""
echo "hcmapfetch: complete"
ls -lh "$DAY_OUT" "$NIGHT_OUT" 2>/dev/null || true

if [[ -n "$CURL"  ]]; then
    echo "spliting curl output"
    cat $TEMP | ./splitcurl $CURL
    cp $TEMP $CURL.raw
	echo "created $CURL.raw $CURL.txt  $CURL.bin"
fi

rm $TEMP