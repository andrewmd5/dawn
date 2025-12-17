//! @file highlight.c
//! @brief Core implementation of the highlight library

#include "highlight.h"
#include "dawn_compat.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

typedef struct {
    pcre2_code* code;
    pcre2_match_data* match_data;
} compiled_pattern_t;

typedef struct {
    const hl_lang_def_t* def;
    compiled_pattern_t* patterns;
    size_t pattern_count;
} compiled_lang_t;

typedef struct lang_entry {
    const hl_lang_def_t* def;
    compiled_lang_t* compiled;
    struct lang_entry* next;
} lang_entry_t;

struct hl_ctx {
    lang_entry_t* languages;
    const hl_theme_t* theme;
    char error_buffer[512];
    bool has_error;
};

static void ctx_set_error(hl_ctx_t* ctx, const char* fmt, ...)
{
    if (!ctx)
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(ctx->error_buffer, sizeof(ctx->error_buffer), fmt, args);
    va_end(args);
    ctx->has_error = true;
}

const char* hl_ctx_get_error(hl_ctx_t* ctx)
{
    if (!ctx)
        return NULL;
    return ctx->has_error ? ctx->error_buffer : NULL;
}

void hl_ctx_clear_error(hl_ctx_t* ctx)
{
    if (!ctx)
        return;
    ctx->has_error = false;
    ctx->error_buffer[0] = '\0';
}

static const char* token_names[] = {
    [HL_TOKEN_NONE] = "none",
    [HL_TOKEN_DELETED] = "deleted",
    [HL_TOKEN_ERR] = "err",
    [HL_TOKEN_VAR] = "var",
    [HL_TOKEN_SECTION] = "section",
    [HL_TOKEN_KWD] = "kwd",
    [HL_TOKEN_CLASS] = "class",
    [HL_TOKEN_CMNT] = "cmnt",
    [HL_TOKEN_INSERT] = "insert",
    [HL_TOKEN_TYPE] = "type",
    [HL_TOKEN_FUNC] = "func",
    [HL_TOKEN_BOOL] = "bool",
    [HL_TOKEN_NUM] = "num",
    [HL_TOKEN_OPER] = "oper",
    [HL_TOKEN_STR] = "str",
    [HL_TOKEN_ESC] = "esc",
};

const char* hl_token_name(hl_token_t token)
{
    if (token < HL_TOKEN_COUNT) {
        return token_names[token];
    }
    return "none";
}

hl_token_t hl_token_from_name(const char* name)
{
    if (!name)
        return HL_TOKEN_NONE;
    for (int32_t i = 0; i < HL_TOKEN_COUNT; i++) {
        if (strcmp(token_names[i], name) == 0) {
            return (hl_token_t)i;
        }
    }
    return HL_TOKEN_NONE;
}

static compiled_pattern_t compile_pattern(const char* pattern, uint32_t flags, hl_ctx_t* ctx)
{
    compiled_pattern_t result = { 0 };

    if (!pattern)
        return result;

    int32_t errorcode;
    PCRE2_SIZE erroroffset;

    uint32_t options = PCRE2_UTF;
    if (flags & HL_RULE_MULTILINE)
        options |= PCRE2_MULTILINE;
    if (flags & HL_RULE_CASELESS)
        options |= PCRE2_CASELESS;
    if (flags & HL_RULE_DOTALL)
        options |= PCRE2_DOTALL;

    result.code = pcre2_compile(
        (PCRE2_SPTR)pattern,
        PCRE2_ZERO_TERMINATED,
        options,
        &errorcode,
        &erroroffset,
        NULL);

    if (!result.code) {
        PCRE2_UCHAR buffer[256];
        pcre2_get_error_message(errorcode, buffer, sizeof(buffer));
        ctx_set_error(ctx, "Regex compilation failed at offset %zu: %s", erroroffset, buffer);
        return result;
    }

    pcre2_jit_compile(result.code, PCRE2_JIT_COMPLETE);
    result.match_data = pcre2_match_data_create_from_pattern(result.code, NULL);

    return result;
}

static void free_compiled_pattern(compiled_pattern_t* pattern)
{
    if (pattern->match_data) {
        pcre2_match_data_free(pattern->match_data);
    }
    if (pattern->code) {
        pcre2_code_free(pattern->code);
    }
}

static compiled_lang_t* compile_language(const hl_lang_def_t* def, hl_ctx_t* ctx)
{
    if (!def || !def->rules)
        return NULL;

    compiled_lang_t* compiled = calloc(1, sizeof(*compiled));
    if (!compiled)
        return NULL;

    compiled->def = def;
    compiled->pattern_count = def->rule_count;
    compiled->patterns = calloc(def->rule_count, sizeof(compiled_pattern_t));

    if (!compiled->patterns) {
        free(compiled);
        return NULL;
    }

    for (size_t i = 0; i < def->rule_count; i++) {
        compiled->patterns[i] = compile_pattern(def->rules[i].pattern, def->rules[i].flags, ctx);
    }

    return compiled;
}

static void free_compiled_lang(compiled_lang_t* lang)
{
    if (!lang)
        return;

    for (size_t i = 0; i < lang->pattern_count; i++) {
        free_compiled_pattern(&lang->patterns[i]);
    }
    free(lang->patterns);
    free(lang);
}

hl_ctx_t* hl_ctx_new(void)
{
    hl_ctx_t* ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;

    ctx->theme = hl_theme_default();
    return ctx;
}

void hl_ctx_free(hl_ctx_t* ctx)
{
    if (!ctx)
        return;

    lang_entry_t* entry = ctx->languages;
    while (entry) {
        lang_entry_t* next = entry->next;
        free_compiled_lang(entry->compiled);
        free(entry);
        entry = next;
    }

    free(ctx);
}

int32_t hl_ctx_register_lang(hl_ctx_t* ctx, const hl_lang_def_t* lang)
{
    if (!ctx || !lang)
        return -1;

    for (lang_entry_t* e = ctx->languages; e; e = e->next) {
        if (e->def == lang || (e->def->name && lang->name && strcmp(e->def->name, lang->name) == 0)) {
            return 0;
        }
    }

    lang_entry_t* entry = calloc(1, sizeof(*entry));
    if (!entry) {
        ctx_set_error(ctx, "Failed to allocate language entry");
        return -1;
    }

    entry->def = lang;
    entry->compiled = compile_language(lang, ctx);
    entry->next = ctx->languages;
    ctx->languages = entry;

    return 0;
}

void hl_ctx_set_theme(hl_ctx_t* ctx, const hl_theme_t* theme)
{
    if (ctx) {
        ctx->theme = theme ? theme : hl_theme_default();
    }
}

static compiled_lang_t* find_language(hl_ctx_t* ctx, const char* name)
{
    if (!ctx || !name)
        return NULL;

    for (lang_entry_t* e = ctx->languages; e; e = e->next) {
        if (e->def->name && strcasecmp(e->def->name, name) == 0) {
            return e->compiled;
        }
        if (e->def->aliases) {
            for (const char* const* alias = e->def->aliases; *alias; alias++) {
                if (strcasecmp(*alias, name) == 0) {
                    return e->compiled;
                }
            }
        }
    }

    return NULL;
}

typedef struct {
    bool valid;
    size_t start;
    size_t end;
} match_cache_t;

typedef struct {
    hl_ctx_t* ctx;
    const char* code;
    size_t code_len;
    hl_token_cb callback;
    void* user_data;
} tokenize_state_t;

static void tokenize_internal(tokenize_state_t* state,
    compiled_lang_t* lang,
    size_t start, size_t end,
    hl_token_t parent_token);

static void emit_token(tokenize_state_t* state,
    size_t start, size_t end,
    hl_token_t token)
{
    if (start < end && state->callback) {
        state->callback(state->code + start, end - start, token, state->user_data);
    }
}

static void tokenize_internal(tokenize_state_t* state,
    compiled_lang_t* lang,
    size_t start, size_t end,
    hl_token_t parent_token)
{
    if (!lang || !lang->patterns || start >= end) {
        emit_token(state, start, end, parent_token);
        return;
    }

    const char* subject = state->code;
    size_t subject_len = end;

    match_cache_t* cache = calloc(lang->pattern_count, sizeof(match_cache_t));
    if (!cache) {
        emit_token(state, start, end, parent_token);
        return;
    }

    bool* active = calloc(lang->pattern_count, sizeof(bool));
    if (!active) {
        free(cache);
        emit_token(state, start, end, parent_token);
        return;
    }
    for (size_t i = 0; i < lang->pattern_count; i++) {
        active[i] = lang->patterns[i].code != NULL;
    }

    size_t pos = start;

    while (pos < end) {
        size_t best_start = SIZE_MAX;
        size_t best_end = 0;
        size_t best_idx = SIZE_MAX;

        for (size_t i = 0; i < lang->pattern_count; i++) {
            if (!active[i])
                continue;

            compiled_pattern_t* pat = &lang->patterns[i];

            if (cache[i].valid && cache[i].start >= pos) {
                if (cache[i].start < best_start || (cache[i].start == best_start && cache[i].end > best_end)) {
                    best_start = cache[i].start;
                    best_end = cache[i].end;
                    best_idx = i;
                }
                continue;
            }

            int32_t rc = pcre2_match(
                pat->code,
                (PCRE2_SPTR)subject,
                subject_len,
                pos,
                0,
                pat->match_data,
                NULL);

            if (rc < 0) {
                active[i] = false;
                cache[i].valid = false;
                continue;
            }

            PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(pat->match_data);
            size_t match_start = ovector[0];
            size_t match_end = ovector[1];

            cache[i].valid = true;
            cache[i].start = match_start;
            cache[i].end = match_end;

            if (match_start < best_start || (match_start == best_start && match_end > best_end)) {
                best_start = match_start;
                best_end = match_end;
                best_idx = i;
            }
        }

        if (best_idx == SIZE_MAX) {
            break;
        }

        if (best_start > pos) {
            emit_token(state, pos, best_start, parent_token);
        }

        const hl_lang_rule_t* rule = &lang->def->rules[best_idx];

        if (rule->sub) {
            compiled_lang_t* sub_lang = find_language(state->ctx, rule->sub->name);
            if (!sub_lang) {
                sub_lang = compile_language(rule->sub, state->ctx);
                if (sub_lang) {
                    lang_entry_t* entry = calloc(1, sizeof(*entry));
                    if (entry) {
                        entry->def = rule->sub;
                        entry->compiled = sub_lang;
                        entry->next = state->ctx->languages;
                        state->ctx->languages = entry;
                    }
                }
            }
            if (sub_lang) {
                tokenize_internal(state, sub_lang, best_start, best_end, rule->sub->default_token);
            } else {
                emit_token(state, best_start, best_end, rule->token);
            }
        } else if (rule->sub_name) {
            compiled_lang_t* sub_lang = find_language(state->ctx, rule->sub_name);
            if (sub_lang) {
                tokenize_internal(state, sub_lang, best_start, best_end, sub_lang->def->default_token);
            } else {
                emit_token(state, best_start, best_end, rule->token);
            }
        } else {
            emit_token(state, best_start, best_end, rule->token);
        }

        pos = best_end;

        for (size_t i = 0; i < lang->pattern_count; i++) {
            if (cache[i].valid && cache[i].end <= pos) {
                cache[i].valid = false;
            }
        }
    }

    if (pos < end) {
        emit_token(state, pos, end, parent_token);
    }

    free(cache);
    free(active);
}

int32_t hl_tokenize(hl_ctx_t* ctx,
    const char* code, size_t code_len,
    const char* lang_name,
    hl_token_cb callback, void* user_data)
{
    if (!ctx || !code || !callback) {
        if (ctx)
            ctx_set_error(ctx, "Invalid arguments to hl_tokenize");
        return -1;
    }

    compiled_lang_t* lang = find_language(ctx, lang_name);
    if (!lang) {
        callback(code, code_len, HL_TOKEN_NONE, user_data);
        return 0;
    }

    tokenize_state_t state = {
        .ctx = ctx,
        .code = code,
        .code_len = code_len,
        .callback = callback,
        .user_data = user_data
    };

    tokenize_internal(&state, lang, 0, code_len, lang->def->default_token);

    return 0;
}

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} string_builder_t;

static void sb_init(string_builder_t* sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static bool sb_ensure(string_builder_t* sb, size_t additional)
{
    size_t needed = sb->len + additional + 1;
    if (needed <= sb->cap)
        return true;

    size_t new_cap = sb->cap ? sb->cap * 2 : 1024;
    while (new_cap < needed)
        new_cap *= 2;

    char* new_data = realloc(sb->data, new_cap);
    if (!new_data)
        return false;

    sb->data = new_data;
    sb->cap = new_cap;
    return true;
}

static bool sb_append(string_builder_t* sb, const char* str, size_t len)
{
    if (!sb_ensure(sb, len))
        return false;
    memcpy(sb->data + sb->len, str, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
    return true;
}

static bool sb_append_str(string_builder_t* sb, const char* str)
{
    return sb_append(sb, str, strlen(str));
}

static void sb_free(string_builder_t* sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

typedef struct {
    string_builder_t sb;
    const hl_theme_t* theme;
} highlight_state_t;

static void highlight_callback(const char* text, size_t len, hl_token_t token, void* user_data)
{
    highlight_state_t* state = user_data;

    if (len == 0)
        return;

    const char* color = state->theme->colors[token];

    if (color && *color) {
        sb_append_str(&state->sb, color);
        sb_append(&state->sb, text, len);
        sb_append_str(&state->sb, state->theme->reset);
    } else {
        sb_append(&state->sb, text, len);
    }
}

char* hl_highlight_ex(hl_ctx_t* ctx,
    const char* code, size_t code_len,
    const char* lang,
    size_t* out_len)
{
    if (!ctx || !code) {
        if (ctx)
            ctx_set_error(ctx, "Invalid arguments to hl_highlight_ex");
        return NULL;
    }

    highlight_state_t state;
    sb_init(&state.sb);
    state.theme = ctx->theme;

    int32_t rc = hl_tokenize(ctx, code, code_len, lang, highlight_callback, &state);
    if (rc < 0) {
        sb_free(&state.sb);
        return NULL;
    }

    if (out_len)
        *out_len = state.sb.len;
    return state.sb.data;
}

bool hl_ctx_lang_supported(hl_ctx_t* ctx, const char* lang)
{
    if (!ctx || !lang || !*lang)
        return false;
    return find_language(ctx, lang) != NULL;
}

static int32_t count_pattern_matches(const char* code, size_t code_len, const char* pattern)
{
    int32_t errorcode;
    PCRE2_SIZE erroroffset;

    pcre2_code* re = pcre2_compile(
        (PCRE2_SPTR)pattern,
        PCRE2_ZERO_TERMINATED,
        PCRE2_UTF | PCRE2_MULTILINE,
        &errorcode,
        &erroroffset,
        NULL);

    if (!re)
        return 0;

    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);
    if (!match_data) {
        pcre2_code_free(re);
        return 0;
    }

    int32_t count = 0;
    size_t offset = 0;

    while (offset < code_len) {
        int32_t rc = pcre2_match(
            re,
            (PCRE2_SPTR)code,
            code_len,
            offset,
            0,
            match_data,
            NULL);

        if (rc < 0)
            break;

        count++;

        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        offset = ovector[1];
        if (offset == ovector[0])
            offset++;
    }

    pcre2_match_data_free(match_data);
    pcre2_code_free(re);

    return count;
}

typedef struct {
    const char* lang;
    int32_t score;
} lang_score_t;

const char* hl_ctx_detect_language(hl_ctx_t* ctx, const char* code, size_t code_len)
{
    if (!ctx || !code || code_len == 0)
        return "plain";

#define MAX_LANGS 64
    lang_score_t scores[MAX_LANGS] = { 0 };
    size_t num_langs = 0;

    for (lang_entry_t* e = ctx->languages; e; e = e->next) {
        const hl_lang_def_t* lang = e->def;
        if (!lang || !lang->detect || lang->detect_count == 0)
            continue;

        for (size_t j = 0; j < lang->detect_count; j++) {
            const hl_detect_rule_t* rule = &lang->detect[j];
            if (!rule->pattern)
                continue;

            int32_t matches = count_pattern_matches(code, code_len, rule->pattern);
            if (matches > 0) {
                int32_t total_score = matches * rule->score;

                size_t idx = SIZE_MAX;
                for (size_t k = 0; k < num_langs; k++) {
                    if (strcmp(scores[k].lang, lang->name) == 0) {
                        idx = k;
                        break;
                    }
                }

                if (idx == SIZE_MAX && num_langs < MAX_LANGS) {
                    idx = num_langs++;
                    scores[idx].lang = lang->name;
                    scores[idx].score = 0;
                }

                if (idx != SIZE_MAX) {
                    scores[idx].score += total_score;
                }
            }
        }
    }

    const char* best_lang = "plain";
    int32_t best_score = 20;

    for (size_t i = 0; i < num_langs; i++) {
        if (scores[i].score > best_score) {
            best_score = scores[i].score;
            best_lang = scores[i].lang;
        }
    }

    return best_lang;
}

hl_ctx_t* hl_ctx_new_with_defaults(bool dark_mode)
{
    hl_ctx_t* ctx = hl_ctx_new();
    if (!ctx)
        return NULL;

    hl_ctx_register_lang(ctx, hl_lang_asm());
    hl_ctx_register_lang(ctx, hl_lang_bash());
    hl_ctx_register_lang(ctx, hl_lang_bf());
    hl_ctx_register_lang(ctx, hl_lang_c());
    hl_ctx_register_lang(ctx, hl_lang_csharp());
    hl_ctx_register_lang(ctx, hl_lang_css());
    hl_ctx_register_lang(ctx, hl_lang_csv());
    hl_ctx_register_lang(ctx, hl_lang_diff());
    hl_ctx_register_lang(ctx, hl_lang_docker());
    hl_ctx_register_lang(ctx, hl_lang_git());
    hl_ctx_register_lang(ctx, hl_lang_go());
    hl_ctx_register_lang(ctx, hl_lang_html());
    hl_ctx_register_lang(ctx, hl_lang_http());
    hl_ctx_register_lang(ctx, hl_lang_ini());
    hl_ctx_register_lang(ctx, hl_lang_java());
    hl_ctx_register_lang(ctx, hl_lang_js());
    hl_ctx_register_lang(ctx, hl_lang_jsdoc());
    hl_ctx_register_lang(ctx, hl_lang_json());
    hl_ctx_register_lang(ctx, hl_lang_js_template());
    hl_ctx_register_lang(ctx, hl_lang_leanpub_md());
    hl_ctx_register_lang(ctx, hl_lang_log());
    hl_ctx_register_lang(ctx, hl_lang_lua());
    hl_ctx_register_lang(ctx, hl_lang_make());
    hl_ctx_register_lang(ctx, hl_lang_md());
    hl_ctx_register_lang(ctx, hl_lang_perl());
    hl_ctx_register_lang(ctx, hl_lang_plain());
    hl_ctx_register_lang(ctx, hl_lang_py());
    hl_ctx_register_lang(ctx, hl_lang_regex());
    hl_ctx_register_lang(ctx, hl_lang_rust());
    hl_ctx_register_lang(ctx, hl_lang_sql());
    hl_ctx_register_lang(ctx, hl_lang_todo());
    hl_ctx_register_lang(ctx, hl_lang_toml());
    hl_ctx_register_lang(ctx, hl_lang_ts());
    hl_ctx_register_lang(ctx, hl_lang_uri());
    hl_ctx_register_lang(ctx, hl_lang_xml());
    hl_ctx_register_lang(ctx, hl_lang_yaml());

    hl_ctx_set_theme(ctx, dark_mode ? hl_theme_atom_dark() : hl_theme_default());
    return ctx;
}
