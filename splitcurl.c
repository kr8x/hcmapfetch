
//
// splitcurl
//
// split a curl output into .txt file for header and .bin file for payload
//
// usage splitcurl filebase
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <name>\n", argv[0]);
        fprintf(stderr, "example: %s example\n", argv[0]);
        fprintf(stderr, "save header in example.txt\nsave content in example.bin\n");		
        return 1;
    }

    char txt_file[256], bin_file[256];
    snprintf(txt_file, sizeof(txt_file), "%s.txt", argv[1]);
    snprintf(bin_file, sizeof(bin_file), "%s.bin", argv[1]);

    FILE *txt = fopen(txt_file, "w");
    FILE *bin = fopen(bin_file, "wb");
    if (!txt || !bin) {
        perror("fopen");
        return 1;
    }

    // Read headers: blank line separates headers from body
    char line[4096];
    int in_headers = 1;

    while (in_headers) {
        // Read one byte at a time to detect \r\n or \n blank line
        char buf[4096];
        int i = 0;
        int c;
        while ((c = getchar()) != EOF) {
            if (c == '\n') {
                buf[i++] = c;
                break;
            }
            buf[i++] = c;
        }
        buf[i] = '\0';

        // Blank line (just \n or \r\n) signals end of headers
        if (strcmp(buf, "\n") == 0 || strcmp(buf, "\r\n") == 0 || i == 0) {
            fwrite(buf, 1, i, txt);  // write the blank line to headers too
            in_headers = 0;
        } else {
            fwrite(buf, 1, i, txt);
        }

        if (c == EOF) {
            in_headers = 0;
        }
    }

    // Rest is binary body
    int c;
    while ((c = getchar()) != EOF) {
        fputc(c, bin);
    }

    fclose(txt);
    fclose(bin);

    printf("Written: %s and %s\n", txt_file, bin_file);
    return 0;
}