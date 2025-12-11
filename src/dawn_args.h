// dawn_args.h

#ifndef DAWN_ARGS_H
#define DAWN_ARGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dawn_types.h"

// #region Argument Types

//! Argument flags
DAWN_ENUM(uint8_t) {
    ARG_DEMO     = 1 << 0,  //!< Demo mode - replay document typing
    ARG_PREVIEW  = 1 << 1,  //!< Read-only preview of file
    ARG_PRINT    = 1 << 2,  //!< Print rendered document to stdout and exit
    ARG_HELP     = 1 << 3,  //!< Show help and exit
    ARG_VERSION  = 1 << 4,  //!< Show version and exit
    ARG_ERROR    = 1 << 5,  //!< Parsing error occurred
    ARG_STDIN    = 1 << 6,  //!< Read from stdin (- operand)
} ArgFlag;

//! Parsed command-line arguments
typedef struct {
    char *file;             //!< Path to file to open (copied to .dawn)
    char *demo_file;        //!< File to replay in demo mode
    const char *error_msg;  //!< Error message
    int8_t theme;           //!< Theme: -1 = not set, 0 = light, 1 = dark
    uint8_t flags;          //!< ArgFlag combination
} DawnArgs;

// #endregion

// #region Functions

//! Parse command-line arguments
//! @param argc argument count from main
//! @param argv argument vector from main
//! @return parsed arguments structure
DawnArgs args_parse(int32_t argc, char *argv[]);

//! Free resources allocated by args_parse
//! @param args pointer to arguments structure
void args_free(DawnArgs *args);

//! Copy a file to the .dawn directory
//! @param src_path source file path
//! @param out_path buffer for destination path
//! @param out_size size of output buffer
//! @return true on success
bool args_copy_to_dawn(const char *src_path, char *out_path, size_t out_size);

//! Print usage information to stderr
void args_print_usage(const char *program_name);

//! Print version information to stdout
void args_print_version(void);

//! Check if stdin has data (for pipe detection)
//! @return true if stdin is a pipe with data
bool args_stdin_has_data(void);

//! Read all content from stdin
//! @param out_size pointer to store the size of returned buffer
//! @return newly allocated buffer with stdin content, or NULL on error
char *args_read_stdin(size_t *out_size);

// #endregion

#endif // DAWN_ARGS_H
