// dawn_args.c

#define _POSIX_C_SOURCE 200809L

#include "dawn_args.h"
#include "dawn_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>

// #region Option Definitions

// POSIX options: single-character only
// -f FILE   Open file for editing
// -d FILE   Demo mode
// -t THEME  Set theme (light/dark)
// -p FILE   Preview file (read-only)
// -P        Print mode (render to stdout)
// -h        Help
// -v        Version
static const char *short_opts = "f:d:t:p:Phv";

// #endregion

// #region Helper Functions

//! Resolve path to absolute
//! @param path input path
//! @return newly allocated absolute path
static char *resolve_path(const char *path) {
    if (!path) return NULL;

    // Already absolute
    if (path[0] == '/') {
        return strdup(path);
    }

    // Home directory expansion
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            size_t len = strlen(home) + strlen(path);  // path includes ~
            char *result = malloc(len);
            if (result) {
                snprintf(result, len, "%s%s", home, path + 1);
            }
            return result;
        }
    }

    // Relative path - resolve from cwd
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd))) {
        size_t len = strlen(cwd) + 1 + strlen(path) + 1;
        char *result = malloc(len);
        if (result) {
            snprintf(result, len, "%s/%s", cwd, path);
        }
        return result;
    }

    return strdup(path);
}

//! Parse theme argument
//! @param arg theme string ("light" or "dark")
//! @return 0 for light, 1 for dark, -1 for invalid
static int32_t parse_theme(const char *arg) {
    if (!arg) return -1;
    if (strcasecmp(arg, "light") == 0 || strcmp(arg, "0") == 0) return 0;
    if (strcasecmp(arg, "dark") == 0 || strcmp(arg, "1") == 0) return 1;
    return -1;
}

// #endregion

// #region Public Functions

DawnArgs args_parse(int32_t argc, char *argv[]) {
    DawnArgs args = {0};
    args.theme = -1;  // Not set

    // Reset getopt
    optind = 1;
    opterr = 0;

    int32_t opt;
    while ((opt = getopt(argc, argv, short_opts)) != -1) {
        switch (opt) {
            case 'f':
                args.file = resolve_path(optarg);
                break;

            case 'd':
                args.flags |= ARG_DEMO;
                args.demo_file = resolve_path(optarg);
                break;

            case 't':
                args.theme = parse_theme(optarg);
                if (args.theme < 0) {
                    args.flags |= ARG_ERROR;
                    args.error_msg = "Invalid theme (use 'light' or 'dark')";
                }
                break;

            case 'p':
                args.flags |= ARG_PREVIEW;
                args.file = resolve_path(optarg);
                break;

            case 'P':
                args.flags |= ARG_PRINT;
                break;

            case 'h':
                args.flags |= ARG_HELP;
                break;

            case 'v':
                args.flags |= ARG_VERSION;
                break;

            case '?':
                args.flags |= ARG_ERROR;
                args.error_msg = "Unknown option";
                break;

            case ':':
                args.flags |= ARG_ERROR;
                args.error_msg = "Missing argument";
                break;
        }
    }

    // Process operands (after options, or after --)
    while (optind < argc) {
        const char *operand = argv[optind++];

        // "-" means stdin
        if (strcmp(operand, "-") == 0) {
            args.flags |= ARG_STDIN;
            continue;
        }

        // File operand (if no file set yet)
        if (!args.file && !args.demo_file) {
            args.file = resolve_path(operand);
        }
    }

    // If print mode with stdin flag or piped input, use stdin
    if (args.flags & ARG_PRINT) {
        if (!(args.flags & ARG_STDIN) && !args.file && args_stdin_has_data()) {
            args.flags |= ARG_STDIN;
        }
    }

    // Auto-detect piped input when no file and no explicit mode
    if (!args.file && !(args.flags & (ARG_DEMO | ARG_HELP | ARG_VERSION | ARG_STDIN))) {
        if (args_stdin_has_data()) {
            args.flags |= ARG_STDIN | ARG_PRINT;
        }
    }

    // Validate combinations
    if ((args.flags & ARG_DEMO) && (args.flags & ARG_PREVIEW)) {
        args.flags |= ARG_ERROR;
        args.error_msg = "Cannot use -d and -p together";
    }

    if ((args.flags & ARG_STDIN) && args.file) {
        args.flags |= ARG_ERROR;
        args.error_msg = "Cannot use - with a file argument";
    }

    if ((args.flags & ARG_PREVIEW) && !args.file) {
        args.flags |= ARG_ERROR;
        args.error_msg = "-p requires a file path";
    }

    if ((args.flags & ARG_PRINT) && !args.file && !(args.flags & ARG_STDIN)) {
        args.flags |= ARG_ERROR;
        args.error_msg = "-P requires a file or stdin input";
    }

    if ((args.flags & ARG_PRINT) && (args.flags & (ARG_PREVIEW | ARG_DEMO))) {
        args.flags |= ARG_ERROR;
        args.error_msg = "Cannot use -P with -p or -d";
    }

    return args;
}

void args_free(DawnArgs *args) {
    if (!args) return;
    free(args->file);
    free(args->demo_file);
    args->file = NULL;
    args->demo_file = NULL;
}

bool args_copy_to_dawn(const char *src_path, char *out_path, size_t out_size) {
    if (!src_path || !out_path || out_size == 0) return false;

    // Get .dawn directory
    const char *home = getenv("HOME");
    if (!home) return false;

    char dawn_dir[512];
    snprintf(dawn_dir, sizeof(dawn_dir), "%s/%s", home, HISTORY_DIR_NAME);

    // Check if file is already in .dawn directory - if so, just use it directly
    if (strncmp(src_path, dawn_dir, strlen(dawn_dir)) == 0) {
        strncpy(out_path, src_path, out_size - 1);
        out_path[out_size - 1] = '\0';
        return true;
    }

    // Ensure .dawn directory exists
    mkdir(dawn_dir, 0755);

    // Generate unique filename based on original name and timestamp
    const char *base = strrchr(src_path, '/');
    base = base ? base + 1 : src_path;

    // Remove .md extension if present for cleaner naming
    char name[256];
    strncpy(name, base, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    char *ext = strrchr(name, '.');
    if (ext && strcasecmp(ext, ".md") == 0) {
        *ext = '\0';
    }

    // Create destination path with timestamp
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(out_path, out_size, "%s/%04d-%02d-%02d_%02d%02d%02d_%s.md",
             dawn_dir, t->tm_year+1900, t->tm_mon+1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec, name);

    // Copy file contents
    FILE *src = fopen(src_path, "r");
    if (!src) return false;

    FILE *dst = fopen(out_path, "w");
    if (!dst) {
        fclose(src);
        return false;
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
    }

    fclose(src);
    fclose(dst);
    return true;
}

void args_print_usage(const char *program_name) {
    fprintf(stderr,
        "Usage: %s [options] [file | -]\n"
        "\n"
        "Dawn: Draft Anything, Write Now\n"
        "A distraction-free writing environment with live markdown rendering\n"
        "\n"
        "Options:\n"
        "  -f file     Open file (copies to ~/.dawn for editing)\n"
        "  -p file     Preview file in read-only mode\n"
        "  -P          Print rendered output to stdout and exit\n"
        "  -d file     Demo mode: replay file as if being typed\n"
        "  -t theme    Set theme: 'light' or 'dark'\n"
        "  -h          Show this help message\n"
        "  -v          Show version information\n"
        "\n"
        "Operands:\n"
        "  file        Path to markdown file (same as -f file)\n"
        "  -           Read from standard input\n"
        "\n"
        "The -- argument terminates option processing.\n"
        "\n"
        "Examples:\n"
        "  %s                       Start with welcome screen\n"
        "  %s notes.md              Open notes.md (copied to ~/.dawn)\n"
        "  %s -p README.md          Preview README.md (read-only)\n"
        "  %s -P doc.md             Print rendered doc.md to stdout\n"
        "  cat doc.md | %s -P       Render piped markdown to stdout\n"
        "  %s -P -                  Read from stdin, print to stdout\n"
        "  %s -t light              Start with light theme\n"
        "  %s -d demo.md -t dark    Demo with dark theme\n"
        "\n",
        program_name, program_name, program_name, program_name,
        program_name, program_name, program_name, program_name, program_name);
}

void args_print_version(void) {
    printf("%s %s\n", APP_NAME, VERSION);
    printf("%s\n", APP_TAGLINE);
}

bool args_stdin_has_data(void) {
    // Check if stdin is a terminal
    if (isatty(STDIN_FILENO)) {
        return false;
    }

    // Check if there's data available
    fd_set fds;
    struct timeval tv = {0, 0};

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

char *args_read_stdin(size_t *out_size) {
    if (!out_size) return NULL;
    *out_size = 0;

    // Initial buffer
    size_t capacity = 4096;
    size_t size = 0;
    char *buf = malloc(capacity);
    if (!buf) return NULL;

    // Read all of stdin
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
        // Grow buffer if needed
        if (size + n + 1 > capacity) {
            capacity = (size + n + 1) * 2;
            char *newbuf = realloc(buf, capacity);
            if (!newbuf) {
                free(buf);
                return NULL;
            }
            buf = newbuf;
        }
        memcpy(buf + size, chunk, n);
        size += n;
    }

    // Null-terminate
    buf[size] = '\0';
    *out_size = size;
    return buf;
}

// #endregion
