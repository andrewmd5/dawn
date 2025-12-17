// dawn_compat.c - Windows/POSIX compatibility implementations

#ifdef _WIN32

#include "dawn_compat.h"
#include <stdio.h>
#include <string.h>

char* optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

int getopt(int argc, char* const argv[], const char* optstring)
{
    static int sp = 1;
    int c;
    const char* cp;

    if (sp == 1) {
        if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0') {
            return -1;
        } else if (strcmp(argv[optind], "--") == 0) {
            optind++;
            return -1;
        }
    }

    optopt = c = argv[optind][sp];
    cp = strchr(optstring, c);
    if (c == ':' || cp == NULL) {
        if (opterr)
            fprintf(stderr, "%s: illegal option -- %c\n", argv[0], c);
        if (argv[optind][++sp] == '\0') {
            optind++;
            sp = 1;
        }
        return '?';
    }

    if (*++cp == ':') {
        if (argv[optind][sp + 1] != '\0') {
            optarg = &argv[optind++][sp + 1];
        } else if (++optind >= argc) {
            if (opterr)
                fprintf(stderr, "%s: option requires an argument -- %c\n", argv[0], c);
            sp = 1;
            return optstring[0] == ':' ? ':' : '?';
        } else {
            optarg = argv[optind++];
        }
        sp = 1;
    } else {
        if (argv[optind][++sp] == '\0') {
            sp = 1;
            optind++;
        }
        optarg = NULL;
    }
    return c;
}

#endif
