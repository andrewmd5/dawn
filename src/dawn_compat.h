// dawn_compat.h - Windows/POSIX compatibility layer

#ifndef DAWN_COMPAT_H
#define DAWN_COMPAT_H

#ifdef _WIN32

#include <io.h>
#include <direct.h>

#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define getcwd _getcwd
#define isatty _isatty
#define fileno _fileno
#define STDIN_FILENO 0

// Minimal getopt implementation for Windows
extern char* optarg;
extern int optind;
extern int opterr;
extern int optopt;

int getopt(int argc, char* const argv[], const char* optstring);

#else

#include <strings.h>
#include <sys/select.h>
#include <unistd.h>

#endif

#endif // DAWN_COMPAT_H
