//! @file highlight.h
//! @brief Fast, lightweight syntax highlighting library with ANSI terminal output
//!
//! A C port of speed-highlight-js, providing regex-based tokenization and
//! colorized output for terminal applications.
//!
//! Example usage:
//! @code
//!     size_t len;
//!     char *result = hl_highlight("int32_t x = 42;", 11, "c", true, &len);
//!     if (result) {
//!         printf("%s\n", result);
//!         free(result);
//!     }
//! @endcode

#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "dawn_support.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HL_VERSION_MAJOR 1
#define HL_VERSION_MINOR 0
#define HL_VERSION_PATCH 0

//! Token type enumeration matching speed-highlight's token classes
DAWN_ENUM(uint8_t) {
    HL_TOKEN_NONE = 0,
    HL_TOKEN_DELETED,
    HL_TOKEN_ERR,
    HL_TOKEN_VAR,
    HL_TOKEN_SECTION,
    HL_TOKEN_KWD,
    HL_TOKEN_CLASS,
    HL_TOKEN_CMNT,
    HL_TOKEN_INSERT,
    HL_TOKEN_TYPE,
    HL_TOKEN_FUNC,
    HL_TOKEN_BOOL,
    HL_TOKEN_NUM,
    HL_TOKEN_OPER,
    HL_TOKEN_STR,
    HL_TOKEN_ESC,
    HL_TOKEN_COUNT
} hl_token_t;

typedef struct hl_lang_rule hl_lang_rule_t;
typedef struct hl_lang_def hl_lang_def_t;

//! Callback for dynamic sub-language selection (like template literals)
//! @param match The matched text
//! @param match_len Length of matched text
//! @return Language definition to use for sub-highlighting, or NULL
typedef const hl_lang_def_t *(*hl_lang_selector_fn)(const char *match, size_t match_len);

//! Language rule - defines how to match and highlight a pattern
struct hl_lang_rule {
    const char *pattern;
    hl_token_t token;
    const hl_lang_def_t *sub;
    const char *sub_name;
    hl_lang_selector_fn sub_selector;
    uint32_t flags;
};

#define HL_RULE_MULTILINE   (1 << 0)
#define HL_RULE_CASELESS    (1 << 1)
#define HL_RULE_DOTALL      (1 << 2)

//! Detection pattern with confidence score
typedef struct {
    const char *pattern;
    int32_t score;
} hl_detect_rule_t;

//! Language definition - array of rules defining a language
struct hl_lang_def {
    const char *name;
    const hl_lang_rule_t *rules;
    size_t rule_count;
    hl_token_t default_token;
    const hl_detect_rule_t *detect;
    size_t detect_count;
};

//! ANSI color codes for a theme
typedef struct {
    const char *name;
    const char *colors[HL_TOKEN_COUNT];
    const char *reset;
} hl_theme_t;

//! Highlight context - holds compiled patterns and configuration
typedef struct hl_ctx hl_ctx_t;

//! Create a new highlight context
//! @return New context, or NULL on failure. Caller must free with hl_ctx_free.
hl_ctx_t *hl_ctx_new(void);

//! Free a highlight context
//! @param ctx Context to free (may be NULL)
void hl_ctx_free(hl_ctx_t *ctx);

//! Register a language definition with the context
//! @param ctx Context
//! @param lang Language definition (must remain valid for context lifetime)
//! @return 0 on success, -1 on failure
int32_t hl_ctx_register_lang(hl_ctx_t *ctx, const hl_lang_def_t *lang);

//! Set the active theme
//! @param ctx Context
//! @param theme Theme to use (must remain valid for context lifetime)
void hl_ctx_set_theme(hl_ctx_t *ctx, const hl_theme_t *theme);

//! Get the last error message from context
//! @param ctx Context
//! @return Error message string, or NULL if no error
const char *hl_ctx_get_error(hl_ctx_t *ctx);

//! Clear the last error in context
//! @param ctx Context
void hl_ctx_clear_error(hl_ctx_t *ctx);

//! Callback function for receiving tokens
//! @param text Token text (not null-terminated)
//! @param len Length of token text
//! @param token Token type
//! @param user_data User-provided data pointer
typedef void (*hl_token_cb)(const char *text, size_t len, hl_token_t token, void *user_data);

//! Tokenize code and call callback for each token
//! @param ctx Highlight context
//! @param code Source code to highlight
//! @param code_len Length of code in bytes
//! @param lang Language identifier (e.g., "js", "c", "py")
//! @param callback Function to call for each token
//! @param user_data User data passed to callback
//! @return 0 on success, -1 on failure
int32_t hl_tokenize(hl_ctx_t *ctx,
                const char *code, size_t code_len,
                const char *lang,
                hl_token_cb callback, void *user_data);

//! Highlight code and return ANSI-colored string
//! @param code Source code to highlight
//! @param code_len Length of code in bytes
//! @param lang Language identifier (e.g., "js", "c", "py") or NULL for auto-detect
//! @param dark_mode true for dark theme (atom-dark), false for light theme (default)
//! @param out_len Output: length of result string (excluding null terminator)
//! @return Newly allocated string with ANSI codes, or NULL on failure. Caller must free.
char *hl_highlight(const char *code, size_t code_len,
                   const char *lang, bool dark_mode,
                   size_t *out_len);

//! Highlight code using a specific context and theme
//! @param ctx Highlight context
//! @param code Source code to highlight
//! @param code_len Length of code in bytes
//! @param lang Language identifier
//! @param out_len Output: length of result string (excluding null terminator)
//! @return Newly allocated string with ANSI codes, or NULL on failure. Caller must free.
char *hl_highlight_ex(hl_ctx_t *ctx,
                      const char *code, size_t code_len,
                      const char *lang,
                      size_t *out_len);

//! Check if a language is registered in a context
//! @param ctx Highlight context
//! @param lang Language identifier (e.g., "js", "c", "py")
//! @return true if the language is registered, false otherwise
bool hl_ctx_lang_supported(hl_ctx_t *ctx, const char *lang);

//! Auto-detect the language of code using registered languages
//! @param ctx Highlight context with registered languages
//! @param code Source code
//! @param code_len Length of code in bytes
//! @return Language identifier string (e.g., "js", "c"), or "plain" if unknown.
const char *hl_ctx_detect_language(hl_ctx_t *ctx, const char *code, size_t code_len);

//! Create a new context with all default languages registered
//! @param dark_mode true for dark theme, false for light theme
//! @return New context. Caller must free with hl_ctx_free.
hl_ctx_t *hl_ctx_new_with_defaults(bool dark_mode);

const hl_lang_def_t *hl_lang_asm(void);
const hl_lang_def_t *hl_lang_bash(void);
const hl_lang_def_t *hl_lang_bf(void);
const hl_lang_def_t *hl_lang_c(void);
const hl_lang_def_t *hl_lang_css(void);
const hl_lang_def_t *hl_lang_csv(void);
const hl_lang_def_t *hl_lang_diff(void);
const hl_lang_def_t *hl_lang_docker(void);
const hl_lang_def_t *hl_lang_git(void);
const hl_lang_def_t *hl_lang_go(void);
const hl_lang_def_t *hl_lang_html(void);
const hl_lang_def_t *hl_lang_http(void);
const hl_lang_def_t *hl_lang_ini(void);
const hl_lang_def_t *hl_lang_java(void);
const hl_lang_def_t *hl_lang_js(void);
const hl_lang_def_t *hl_lang_jsdoc(void);
const hl_lang_def_t *hl_lang_json(void);
const hl_lang_def_t *hl_lang_js_template(void);
const hl_lang_def_t *hl_lang_leanpub_md(void);
const hl_lang_def_t *hl_lang_log(void);
const hl_lang_def_t *hl_lang_lua(void);
const hl_lang_def_t *hl_lang_make(void);
const hl_lang_def_t *hl_lang_md(void);
const hl_lang_def_t *hl_lang_perl(void);
const hl_lang_def_t *hl_lang_plain(void);
const hl_lang_def_t *hl_lang_py(void);
const hl_lang_def_t *hl_lang_regex(void);
const hl_lang_def_t *hl_lang_rust(void);
const hl_lang_def_t *hl_lang_sql(void);
const hl_lang_def_t *hl_lang_todo(void);
const hl_lang_def_t *hl_lang_toml(void);
const hl_lang_def_t *hl_lang_ts(void);
const hl_lang_def_t *hl_lang_uri(void);
const hl_lang_def_t *hl_lang_xml(void);
const hl_lang_def_t *hl_lang_yaml(void);

const hl_theme_t *hl_theme_default(void);
const hl_theme_t *hl_theme_atom_dark(void);

//! Get the name of a token type
//! @param token Token type
//! @return Static string name (e.g., "kwd", "str"), or "none" for HL_TOKEN_NONE
const char *hl_token_name(hl_token_t token);

//! Get a token type from its name
//! @param name Token name (e.g., "kwd", "str")
//! @return Token type, or HL_TOKEN_NONE if not found
hl_token_t hl_token_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif // HIGHLIGHT_H
