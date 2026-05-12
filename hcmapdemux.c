/*
 * hcmapdemux.c
 *
 * Demux HamClock map downloads from stdin (piped from curl -s -i).
 *
 * Supports two protocols:
 *   v4.22+  zlib-compressed concatenated streams, lengths in X-2Z-lengths HTTP header
 *   v3.10   raw concatenated BMP files, size read from BMP filesize field
 *
 * Build:
 *   make
 *
 * Usage:
 *  see hcmapfetch.sh 
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <zlib.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define BPERBMPPIX       3      /* 24-bit BMP: 3 bytes per pixel           */
#define COPY_BUF_SIZE 16384     /* I/O chunk size                          */
#define ZOUT_BUF_SIZE 65536     /* zlib output buffer                      */
#define MAX_BMP_HDR   1024      /* max plausible BMP header size           */

/* ------------------------------------------------------------------ */
/*  Types                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    int  is_v4;     /* 1 = v4.22 zlib protocol, 0 = v3.10 raw BMP */
    long l1;        /* compressed byte count, stream 1 (v4 only)   */
    long l2;        /* compressed byte count, stream 2 (v4 only)   */
} MapProtocol;

/* ------------------------------------------------------------------ */
/*  HTTP header parsing                                                */
/* ------------------------------------------------------------------ */

/*
 * Consume HTTP headers from `in`.
 * Detects the X-2Z-lengths header produced by HamClock v4.22+.
 * Stops at the blank line that ends the header block.
 */
static MapProtocol parse_headers(FILE *in)
{
    MapProtocol p = { 0, 0, 0 };
    char line[1024];

    while (fgets(line, sizeof(line), in)) {
        /* blank line = end of HTTP headers */
        if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0)
            break;

        /* v4.22 zlib marker */
        if (strncasecmp(line, "X-2Z-lengths:", 13) == 0) {
            if (sscanf(line + 13, " %ld %ld", &p.l1, &p.l2) == 2)
                p.is_v4 = 1;
        }
    }

    return p;
}

/* ------------------------------------------------------------------ */
/*  BMP header validation                                              */
/* ------------------------------------------------------------------ */

/*
 * Read and validate a variable-length BMP header from `in`.
 * Writes the complete header to `out`.
 * Height may be negative (top-down BMP) — comparison uses abs(height).
 * Optionally checks width/height if both exp_w and exp_h are > 0.
 * Returns 1 on success, 0 on failure.
 * Sets *filesize_out to the BMP filesize field.
 * Sets *hdrsize_out to the pixel data offset (= full header size).
 */
static int read_validate_bmp_header(FILE *in, FILE *out,
                                    int exp_w, int exp_h,
                                    uint32_t *filesize_out,
                                    uint32_t *hdrsize_out)
{
    /* Read the base 14-byte BMP file header to get pixel data offset */
    unsigned char fhdr[14];
    if (fread(fhdr, 1, 14, in) != 14) {
        fprintf(stderr, "hcmapdemux: short BMP file header\n");
        return 0;
    }

    if (fhdr[0] != 'B' || fhdr[1] != 'M') {
        fprintf(stderr, "hcmapdemux: bad BMP magic 0x%02x 0x%02x "
                        "(server may have returned an error page)\n",
                fhdr[0], fhdr[1]);
        return 0;
    }

    /* filesize at offset 2 (little-endian uint32) */
    uint32_t fs = (uint32_t) fhdr[2]
                | ((uint32_t)fhdr[3] <<  8)
                | ((uint32_t)fhdr[4] << 16)
                | ((uint32_t)fhdr[5] << 24);

    /* pixel data offset at offset 10 — equals the full header size */
    uint32_t px_offset = (uint32_t) fhdr[10]
                       | ((uint32_t)fhdr[11] <<  8)
                       | ((uint32_t)fhdr[12] << 16)
                       | ((uint32_t)fhdr[13] << 24);

    if (px_offset < 14 || px_offset > MAX_BMP_HDR) {
        fprintf(stderr, "hcmapdemux: implausible pixel offset %u\n", px_offset);
        return 0;
    }

    /* Read the rest of the header (variable length DIB header + palette) */
    uint32_t remain = px_offset - 14;
    unsigned char rest[MAX_BMP_HDR];
    if (fread(rest, 1, remain, in) != remain) {
        fprintf(stderr, "hcmapdemux: short BMP info header\n");
        return 0;
    }

    /* Width  at BMP offset 18 = rest[4..7]  */
    int32_t bw = (int32_t)( rest[4]
               | ((uint32_t)rest[5] <<  8)
               | ((uint32_t)rest[6] << 16)
               | ((uint32_t)rest[7] << 24));

    /* Height at BMP offset 22 = rest[8..11] — negative means top-down */
    int32_t bh = (int32_t)( rest[8]
               | ((uint32_t)rest[9]  <<  8)
               | ((uint32_t)rest[10] << 16)
               | ((uint32_t)rest[11] << 24));

    int abs_h = (bh < 0) ? -bh : bh;

    if (exp_w > 0 && exp_h > 0) {
        if (bw != exp_w || abs_h != exp_h) {
            fprintf(stderr,
                    "hcmapdemux: BMP dimensions %dx%d (abs height %d) "
                    "don't match expected %dx%d\n",
                    bw, bh, abs_h, exp_w, exp_h);
            return 0;
        }
    }

    fprintf(stderr,
            "hcmapdemux: BMP header OK  width=%d height=%d (abs %d)  "
            "hdr=%u bytes  filesize=%u\n",
            bw, bh, abs_h, px_offset, fs);

    if (filesize_out) *filesize_out = fs;
    if (hdrsize_out)  *hdrsize_out  = px_offset;

    /* Write the complete variable-length header */
    fwrite(fhdr, 1, 14,     out);
    fwrite(rest, 1, remain, out);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  v3.10 — raw BMP copy                                               */
/* ------------------------------------------------------------------ */

/*
 * Read one raw BMP from `in`, write it to `out_path`.
 * Byte count is taken from the BMP filesize field — no pre-calculation needed.
 * exp_w / exp_h are used only for optional dimension validation.
 */
static int copy_raw_bmp(FILE *in, int exp_w, int exp_h,
                        const char *out_path)
{
    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "hcmapdemux: cannot create %s: %s\n",
                out_path, strerror(errno));
        return 0;
    }

    uint32_t filesize = 0, hdrsize = 0;
    if (!read_validate_bmp_header(in, out, exp_w, exp_h, &filesize, &hdrsize)) {
        fclose(out);
        return 0;
    }

    if (filesize < hdrsize) {
        fprintf(stderr, "hcmapdemux: BMP filesize %u < header size %u\n",
                filesize, hdrsize);
        fclose(out);
        return 0;
    }

    unsigned char buf[COPY_BUF_SIZE];
    long remaining = (long)filesize - (long)hdrsize;

    while (remaining > 0) {
        size_t to_read = (remaining < (long)sizeof(buf))
                         ? (size_t)remaining : sizeof(buf);
        size_t got = fread(buf, 1, to_read, in);
        if (got == 0) {
            if (feof(in))
                fprintf(stderr, "hcmapdemux: unexpected EOF, %ld bytes short\n",
                        remaining);
            else
                fprintf(stderr, "hcmapdemux: read error: %s\n", strerror(errno));
            fclose(out);
            return 0;
        }
        fwrite(buf, 1, got, out);
        remaining -= (long)got;
    }

    fclose(out);
    fprintf(stderr, "hcmapdemux: wrote %s (%u bytes)\n", out_path, filesize);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  v4.22 — zlib decompression                                         */
/* ------------------------------------------------------------------ */

/*
 * Read exactly `compressed_len` bytes from `in`, decompress with zlib,
 * and write the resulting BMP to `out_path`.
 *
 * Auto-detects zlib framing (first byte 0x78) vs raw deflate.
 * Validates decompressed BMP header (variable length, abs height).
 */
static int decompress_zstream(FILE *in, long compressed_len,
                               int exp_w, int exp_h,
                               const char *out_path)
{
    /* Peek at the first byte to decide framing */
    int first = fgetc(in);
    if (first == EOF) {
        fprintf(stderr, "hcmapdemux: empty stream for %s\n", out_path);
        return 0;
    }
    ungetc(first, in);

    int window_bits = (first == 0x78) ? 15 : -15; /* zlib wrapper or raw deflate */

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, window_bits) != Z_OK) {
        fprintf(stderr, "hcmapdemux: inflateInit2 failed\n");
        return 0;
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "hcmapdemux: cannot create %s: %s\n",
                out_path, strerror(errno));
        inflateEnd(&zs);
        return 0;
    }

    unsigned char in_buf[COPY_BUF_SIZE];
    unsigned char out_buf[ZOUT_BUF_SIZE];
    long     remaining  = compressed_len;
    long     total_out  = 0;
    int      zret       = Z_OK;

    /* BMP header accumulation — need at least 14 bytes to know px_offset */
    unsigned char hdr_buf[MAX_BMP_HDR];
    int  hdr_bytes  = 0;    /* decompressed bytes accumulated into hdr_buf */
    int  hdr_needed = 14;   /* grows to px_offset once we read first 14 bytes */
    int  bmp_ok     = 0;    /* set once header validated and written */

    while (remaining > 0 && zret != Z_STREAM_END) {
        size_t to_read = (remaining < (long)sizeof(in_buf))
                         ? (size_t)remaining : sizeof(in_buf);
        size_t got = fread(in_buf, 1, to_read, in);
        if (got == 0) {
            fprintf(stderr, "hcmapdemux: read error or short stream\n");
            break;
        }
        remaining -= (long)got;

        zs.next_in  = in_buf;
        zs.avail_in = (uInt)got;

        do {
            zs.next_out  = out_buf;
            zs.avail_out = sizeof(out_buf);
            zret = inflate(&zs, Z_NO_FLUSH);

            if (zret != Z_OK && zret != Z_STREAM_END) {
                fprintf(stderr, "hcmapdemux: zlib error %d: %s\n",
                        zret, zs.msg ? zs.msg : "unknown");
                goto zlib_done;
            }

            size_t produced = sizeof(out_buf) - zs.avail_out;
            total_out += (long)produced;

            if (!bmp_ok) {
                /* Feed bytes into hdr_buf until we have the full header */
                size_t src_pos = 0;

                while (src_pos < produced && hdr_bytes < hdr_needed) {
                    size_t space = (size_t)(hdr_needed - hdr_bytes);
                    size_t avail = produced - src_pos;
                    size_t take  = (avail < space) ? avail : space;
                    memcpy(hdr_buf + hdr_bytes, out_buf + src_pos, take);
                    hdr_bytes += (int)take;
                    src_pos   += take;

                    /* Once we have 14 bytes we know the true header size */
                    if (hdr_bytes == 14 && hdr_needed == 14) {
                        uint32_t px_offset =
                              (uint32_t) hdr_buf[10]
                            | ((uint32_t)hdr_buf[11] <<  8)
                            | ((uint32_t)hdr_buf[12] << 16)
                            | ((uint32_t)hdr_buf[13] << 24);
                        if (px_offset < 14 || px_offset > MAX_BMP_HDR) {
                            fprintf(stderr,
                                    "hcmapdemux: bad px_offset %u in "
                                    "decompressed stream\n", px_offset);
                            goto zlib_done;
                        }
                        hdr_needed = (int)px_offset;
                    }
                }

                if (hdr_bytes >= hdr_needed) {
                    /* Full header accumulated — validate */
                    if (hdr_buf[0] != 'B' || hdr_buf[1] != 'M') {
                        fprintf(stderr,
                                "hcmapdemux: decompressed data is not a BMP\n");
                        goto zlib_done;
                    }

                    int32_t bw = (int32_t)( hdr_buf[18]
                                | ((uint32_t)hdr_buf[19] <<  8)
                                | ((uint32_t)hdr_buf[20] << 16)
                                | ((uint32_t)hdr_buf[21] << 24));
                    int32_t bh = (int32_t)( hdr_buf[22]
                                | ((uint32_t)hdr_buf[23] <<  8)
                                | ((uint32_t)hdr_buf[24] << 16)
                                | ((uint32_t)hdr_buf[25] << 24));
                    int abs_h = (bh < 0) ? -bh : bh;

                    if (exp_w > 0 && exp_h > 0 &&
                        (bw != exp_w || abs_h != exp_h)) {
                        fprintf(stderr,
                                "hcmapdemux: decompressed BMP %dx%d (abs %d) "
                                "!= expected %dx%d\n",
                                bw, bh, abs_h, exp_w, exp_h);
                        goto zlib_done;
                    }

                    fprintf(stderr,
                            "hcmapdemux: decompressed BMP OK  "
                            "width=%d height=%d (abs %d)  hdr=%d bytes\n",
                            bw, bh, abs_h, hdr_needed);

                    fwrite(hdr_buf, 1, (size_t)hdr_needed, out);

                    /* write any bytes decompressed past the header */
                    if (src_pos < produced)
                        fwrite(out_buf + src_pos, 1, produced - src_pos, out);

                    bmp_ok = 1;
                }
                /* else: still accumulating header bytes */

            } else {
                fwrite(out_buf, 1, produced, out);
            }

        } while (zs.avail_out == 0);
    }

zlib_done:
    inflateEnd(&zs);
    fclose(out);

    if (zret != Z_STREAM_END && zret != Z_OK) {
        fprintf(stderr, "hcmapdemux: incomplete decompression for %s\n", out_path);
        return 0;
    }

    fprintf(stderr, "hcmapdemux: wrote %s (%ld bytes decompressed)\n",
            out_path, total_out);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Usage                                                              */
/* ------------------------------------------------------------------ */

static void usage(const char *argv0)
{
    fprintf(stderr,
        "\nUsage: %s [options]\n"
        "\n"
        "Read a HamClock map HTTP response from stdin and split it into\n"
        "two BMP files (day map and night map).\n"
        "\n"
        "Options:\n"
        "  -x WIDTH    expected map pixel width  (optional, used for validation)\n"
        "  -y HEIGHT   expected map pixel height (optional, used for validation)\n"
        "  -d FILE     day   map output filename (default: day_map.bmp)\n"
        "  -n FILE     night map output filename (default: night_map.bmp)\n"
        "  --help      show this message\n"
        "\n"
        "Examples:\n"
        "  # v4.22+ zlib protocol\n"
        "  curl -s -i 'http://hamclock:8080/map' | %s\n"
        "\n"
        "  # v3.10 raw BMP protocol\n"
        "  curl -s -i -A 'HamClock-linux/v3.10' 'http://hamclock:8080/map' \\\n"
        "      | %s -x 660 -y 330\n"
        "\n"
        "Build:\n"
        "  gcc -O2 -o hcmapdemux hcmapdemux.c -lz\n"
        "\n",
        argv0, argv0, argv0);
    exit(1);
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int         width     = 0;
    int         height    = 0;
    const char *day_out   = "day_map.bmp";
    const char *night_out = "night_map.bmp";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
        } else if (!strcmp(argv[i], "-x") && i + 1 < argc) {
            width = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-y") && i + 1 < argc) {
            height = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            day_out = argv[++i];
        } else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            night_out = argv[++i];
        } else {
            fprintf(stderr, "hcmapdemux: unknown option: %s\n", argv[i]);
            usage(argv[0]);
        }
    }

    MapProtocol p = parse_headers(stdin);

    if (p.is_v4) {
        fprintf(stderr,
                "hcmapdemux: protocol v4.22 zlib  "
                "stream1=%ld  stream2=%ld compressed bytes\n",
                p.l1, p.l2);

        if (!decompress_zstream(stdin, p.l1, width, height, day_out))
            return 1;
        if (!decompress_zstream(stdin, p.l2, width, height, night_out))
            return 1;

    } else {
        fprintf(stderr, "hcmapdemux: protocol v3.10 raw BMP\n");

        /* File size is read from the BMP filesize field — no pre-calculation.
           -x / -y are used only for optional dimension validation.            */
        if (!copy_raw_bmp(stdin, width, height, day_out))
            return 1;
        if (!copy_raw_bmp(stdin, width, height, night_out))
            return 1;
    }

    fprintf(stderr, "hcmapdemux: done\n");
    return 0;
}