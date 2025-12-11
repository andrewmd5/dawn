// main_term.c - Terminal Frontend for Dawn

#include "dawn_app.h"
#include "dawn_args.h"
#include "dawn_backend.h"
#include <stdio.h>
#include <stdlib.h>

extern const DawnBackend dawn_backend_posix;

int32_t main(int32_t argc, char *argv[]) {
    // Parse command-line arguments
    DawnArgs args = args_parse(argc, argv);

    // Handle errors
    if (args.flags & ARG_ERROR) {
        fprintf(stderr, "dawn: %s\n", args.error_msg);
        args_print_usage(argv[0]);
        args_free(&args);
        return 1;
    }

    // Handle help/version (no backend needed)
    if (args.flags & ARG_HELP) {
        args_print_usage(argv[0]);
        args_free(&args);
        return 0;
    }

    if (args.flags & ARG_VERSION) {
        args_print_version();
        args_free(&args);
        return 0;
    }

    // Read stdin if needed (before initializing backend)
    char *stdin_content = NULL;
    size_t stdin_size = 0;
    if (args.flags & ARG_STDIN) {
        stdin_content = args_read_stdin(&stdin_size);
        if (!stdin_content || stdin_size == 0) {
            fprintf(stderr, "dawn: no input on stdin\n");
            args_free(&args);
            return 1;
        }
    }

    // Determine mode
    DawnMode mode = (args.flags & ARG_PRINT)
        ? DAWN_MODE_PRINT
        : DAWN_MODE_INTERACTIVE;

    // Initialize context with backend
    if (!dawn_ctx_init(&app.ctx, &dawn_backend_posix, mode)) {
        fprintf(stderr, "dawn: failed to initialize backend\n");
        free(stdin_content);
        args_free(&args);
        return 1;
    }

    // Determine theme (command-line overrides default)
    Theme theme = (args.theme >= 0) ? (Theme)args.theme : THEME_DARK;

    // Initialize Dawn engine
    if (!dawn_engine_init(theme)) {
        fprintf(stderr, "dawn: failed to initialize engine\n");
        dawn_ctx_shutdown(&app.ctx);
        free(stdin_content);
        args_free(&args);
        return 1;
    }

    // Handle print mode
    if (args.flags & ARG_PRINT) {
        bool ok;
        if (stdin_content) {
            ok = dawn_print_buffer(stdin_content, stdin_size);
        } else {
            ok = dawn_print_document(args.file);
        }
        if (!ok) {
            fprintf(stderr, "dawn: cannot process input\n");
            dawn_engine_shutdown();
            dawn_ctx_shutdown(&app.ctx);
            free(stdin_content);
            args_free(&args);
            return 1;
        }
        dawn_engine_shutdown();
        dawn_ctx_shutdown(&app.ctx);
        free(stdin_content);
        args_free(&args);
        return 0;
    }

    // Handle preview mode
    if (args.flags & ARG_PREVIEW) {
        bool ok;
        if (stdin_content) {
            ok = dawn_preview_buffer(stdin_content, stdin_size);
        } else {
            ok = dawn_preview_document(args.file);
        }
        if (!ok) {
            fprintf(stderr, "dawn: cannot preview input\n");
            dawn_engine_shutdown();
            dawn_ctx_shutdown(&app.ctx);
            free(stdin_content);
            args_free(&args);
            return 1;
        }
    } else if (args.file) {
        // Edit mode: copy to .dawn directory
        char dest_path[512];
        if (args_copy_to_dawn(args.file, dest_path, sizeof(dest_path))) {
            dawn_load_document(dest_path);
        } else {
            fprintf(stderr, "dawn: cannot open file: %s\n", args.file);
            dawn_engine_shutdown();
            dawn_ctx_shutdown(&app.ctx);
            free(stdin_content);
            args_free(&args);
            return 1;
        }
    }

    free(stdin_content);
    args_free(&args);

    // Main loop (interactive mode only)
    while (dawn_frame()) {
        DAWN_BACKEND(app)->input_ready(6.944f);
        if (DAWN_BACKEND(app)->poll_jobs) {
            DAWN_BACKEND(app)->poll_jobs();
        }
    }

    // Cleanup
    dawn_engine_shutdown();
    dawn_ctx_shutdown(&app.ctx);

    return 0;
}
