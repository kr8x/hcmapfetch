# hcmapfetch

Fetch and demux HamClock VOACAP propagation area maps from the
backend server for HamClock such as clearskyinstitute.com, hamclock.com or ohb.hamclock.app

Optionally take the server response and split out the returned headers and body

Written by David Strickland KR8X.  
Based on HamClock by Elwood Downey WB0OEW.  
Licensed under the MIT License — see [LICENSE](LICENSE).

---

## What it does

HamClock serves propagation map responses as two concatenated image streams
(day map and night map) in one HTTP response.  This package fetches that
response and splits it into two separate BMP files.

Two server protocols are supported that can be selected with option -V 

| Version | Protocol | Detection |
|---|---|---|
| v4.xx+ | Two zlib-compressed streams | `X-2Z-lengths` HTTP header present |
| v3.xx  | Two raw BMP files concatenated | No `X-2Z-lengths` header |

---

## Quick start

```bash
cd
mkdir hcmapfetch
cd hcmapfetch
git clone  https://github.com/kr8x/hcmapfetch.git
make
chmod +x hcmapfetch.sh
```

This builds support programs `splitcurl` and  `hcmapdemux` (which uses the bundled `zlib-hc` library).
The shell script `hcmapfetch.sh` is ready to use as-is — no installation required.

---

## Files

| File | Description |
|---|---|
| `hcmapfetch.sh` | Shell script — builds the URL, calls curl, pipes to hcmapdemux |
| `hcmapdemux.c` | C program — reads the HTTP response from stdin, splits the two BMPs |
| `hcmapdemux.c` | C program — takes the HRRP response from stdin and extracts the HTTP headers and body |
| `Makefile` | Builds support programs |
| `zlib-hc/` | Bundled HamClock zlib library |
| `LICENSE` | MIT License |
| `README.md | this file |

---

## Build

```bash
make
```

To remove all build artifacts including the binary:

```bash
make clobber
```

`make clean` and `make clobber` are equivalent — both remove object files,
the `hcmapdemux` binary, any downloaded .bmp files and clean the `zlib-hc` sub-library.

---

## Usage

### hcmapfetch.sh

```
Usage: hcmapfetch.sh [options]

Connection:
  -s HOST       HamClock server host or IP         (default: clearskyinstitute.com)
  -p PORT       HamClock server port               (default: 80)
  -V VERSION    HamClock version use  4.22 (zlib, default) or 3.10 (raw BMP)

TX station:
  -t TXLAT      Transmitter latitude               (default: 33.167)
  -T TXLNG      Transmitter longitude              (default: -96.917)

VOACAP parameters:
  -m MODE       Propagation mode number            (default: 19) 19 is CW see --help
  -f MHZ        Frequency in MHz                   (default: 3.60)
  -w WATTS      TX power in watts                  (default: 100)
  -o TOA        Take-off angle                     (default: 3.0)
  -r REQ        Map Request Type REL,TOA,MUF       (defualt: REL)
  -A ARG        Use to add an additional argument like CAPTION-

Map dimensions:
  -x WIDTH      Map pixel width                    (default: 660)
  -y HEIGHT     Map pixel height                   (default: 330)

Output:
  -d FILE       Day   map output filename          (default: day_map.bmp)
  -n FILE       Night map output filename          (default: night_map.bmp)
  -b BINARY     folder containing support programs (default: .)

  --help        Show this help
```

### Examples

```bash
# v4.22+ server, defaults (REL, 3.60 MHz, 100W)
./hcmapfetch.sh

# v3.10 server
./hcmapfetch.sh -V 3.10

# Public ClearSky server, 14 MHz, CW mode 19
./hcmapfetch.sh -s clearskyinstitute.com -p 80 -V 3.10 -f 14.10 -m 19

# Custom TX location and output filenames
./hcmapfetch.sh -s 192.168.1.50 -t 29.786 -T -95.389 -d /tmp/day.bmp -n /tmp/night.bmp
```

### hcmapdemux (standalone)

`hcmapdemux` reads a raw HTTP response from stdin and writes two BMP files.
It is normally invoked by `hcmapfetch.sh` but can be used directly. For
values of URL see hcmapfetch.sh

```bash
# v4.22+ zlib protocol — no dimensions needed
curl -s -i URL | ./hcmapdemux

# v3.10 raw BMP — dimensions are optional, used for validation only
curl -s -i -A "HamClock-linux/3.10" URL | ./hcmapdemux -x 660 -y 330

# Custom output filenames
curl -s -i URL | ./hcmapdemux -d day.bmp -n night.bmp
```

```
hcmapdemux options:
  -x WIDTH    expected map pixel width  (optional, for validation)
  -y HEIGHT   expected map pixel height (optional, for validation)
  -d FILE     day   map output          (default: day_map.bmp)
  -n FILE     night map output          (default: night_map.bmp)
  --help      show usage
```

---

## BMP notes

The server returns 24-bit BMP files with a variable-length header
(BITMAPV4HEADER, 108 bytes, rather than the basic 54-byte BITMAPINFOHEADER).
Height is encoded as a negative value indicating a top-down pixel layout.
`hcmapdemux` handles both header sizes and both height signs automatically.

---

## Acknowledgements

- **HamClock** by Elwood Downey WB0OEW 
- **zlib** by Jean-loup Gailly and Mark Adler — https://zlib.net/
