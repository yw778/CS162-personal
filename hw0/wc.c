#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>

typedef unsigned long long wc_count_t;

wc_count_t tot_line_cnt, tot_word_cnt, tot_char_cnt;


void wc(char*);
void print_wc(wc_count_t, wc_count_t, wc_count_t, char*);

int main(int argc, char *argv[]) {
    if (argc == 1) {
        wc(NULL);
    } else {
        int tmp = argc;
        while(argc > 1) {
            wc(*++argv);
            argc--;
        }
        if (tmp > 2) {
            print_wc(tot_line_cnt, tot_word_cnt, tot_char_cnt, "total");
        }
    }
    return 0;
}

void wc (char* filename) {
    int fd, len;
    char* name;
    char ch;
    wc_count_t line_cnt, word_cnt, char_cnt;
    line_cnt = word_cnt = char_cnt = 0;

    if (!filename) {
        fd = stdin;
        name = "<stdin>";
    } else {
        if ((fd = open(filename, O_RDONLY)) < 0) {
            fprintf(stderr, "wc: %s: No such file or directory\n", filename);
            exit(1);
        }
        name = filename;
    }

    while((len = read(fd, &ch, 1)) == 1) {
        char_cnt++;
        int space = 1;
        if (isspace(ch)) {
            if (ch == '\n') {
                line_cnt++;
            }
            space = 1;
        } else {
            if (space) {
                space = 0;
                word_cnt++;
            }
        }
    }

    tot_char_cnt += char_cnt;
    tot_word_cnt += word_cnt;
    tot_line_cnt += line_cnt;
    print_wc(line_cnt, word_cnt, char_cnt, name);

    if (close(fd) == -1) {
        fprintf(stderr, "wc: %s: close file error\n", name);
        exit(1);
    }
}

void print_wc(wc_count_t line_cnt, wc_count_t word_cnt, wc_count_t char_cnt, char* name) {
    printf("%7llu %7llu %7llu", line_cnt, word_cnt, char_cnt);
    if (name) {
        printf(" %s\n", name);
    } else {
        printf("\n");
    }
}


