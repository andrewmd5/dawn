// dawn_tex.c

#include "dawn_tex.h"
#include "dawn_utils.h"
#include "utf8proc/utf8proc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations from dawn_tex_symbols.c
extern const char *tex_get_alphabet(TexFontStyle style);
extern const char *tex_to_superscript(const char *c);
extern const char *tex_to_subscript(const char *c);
extern const char *tex_lookup_symbol(const char *name);
extern const char *tex_get_accent(const char *name);
extern TexFontStyle tex_get_font_style(const char *name);
extern const char *tex_get_multiline_op(const char *name, int32_t *out_height, int32_t *out_width, int32_t *out_horizon);
extern const char *tex_get_delimiter_char(char delim, TexDelimPos position);
extern char tex_revert_font_char(const char *utf8_char);
extern const char *tex_unshrink_char(const char *utf8_char);

// #region Background Character

static const char *TEX_BG = " ";

// #endregion

// #region Dynamic Arrays

static void id_array_init(TexIdArray *arr) {
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void id_array_push(TexIdArray *arr, int32_t val) {
    if (arr->count >= arr->capacity) {
        int32_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        arr->data = realloc(arr->data, new_cap * sizeof(int32_t));
        arr->capacity = new_cap;
    }
    arr->data[arr->count++] = val;
}

static void id_array_free(TexIdArray *arr) {
    free(arr->data);
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

// #endregion

// #region Sketch Management

//! Duplicate a UTF-8 string
static char *tex_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (dup) memcpy(dup, s, len + 1);
    return dup;
}

static void row_init(TexRow *row) {
    row->cells = NULL;
    row->count = 0;
    row->capacity = 0;
}

static void row_push(TexRow *row, const char *ch) {
    if (row->count >= row->capacity) {
        int32_t new_cap = row->capacity == 0 ? TEX_INITIAL_ROW_CAPACITY : row->capacity * 2;
        row->cells = realloc(row->cells, new_cap * sizeof(TexCell));
        row->capacity = new_cap;
    }
    row->cells[row->count].data = tex_strdup(ch);
    row->count++;
}

static void row_free(TexRow *row) {
    for (int32_t i = 0; i < row->count; i++) {
        free(row->cells[i].data);
    }
    free(row->cells);
    row->cells = NULL;
    row->count = 0;
    row->capacity = 0;
}

TexSketch *tex_sketch_new(int32_t height, int32_t width) {
    TexSketch *s = calloc(1, sizeof(TexSketch));
    if (!s) return NULL;

    s->height = height;
    s->width = width;
    s->horizon = 0;
    s->rows = calloc(height, sizeof(TexRow));

    for (int32_t i = 0; i < height; i++) {
        row_init(&s->rows[i]);
        for (int32_t j = 0; j < width; j++) {
            row_push(&s->rows[i], TEX_BG);
        }
    }

    return s;
}

void tex_sketch_free(TexSketch *s) {
    if (!s) return;
    if (s->rows) {
        for (int32_t i = 0; i < s->height; i++) {
            row_free(&s->rows[i]);
        }
        free(s->rows);
    }
    free(s);
}

void tex_sketch_print(const TexSketch *s) {
    if (!s) return;
    for (int32_t i = 0; i < s->height; i++) {
        for (int32_t j = 0; j < s->rows[i].count; j++) {
            printf("%s", s->rows[i].cells[j].data ? s->rows[i].cells[j].data : " ");
        }
        if (i == s->horizon) printf(" <--");
        printf("\n");
    }
}

char *tex_sketch_to_string(const TexSketch *s) {
    if (!s) return tex_strdup("");

    // Calculate total size needed
    size_t total = 0;
    for (int32_t i = 0; i < s->height; i++) {
        for (int32_t j = 0; j < s->rows[i].count; j++) {
            if (s->rows[i].cells[j].data) {
                total += strlen(s->rows[i].cells[j].data);
            }
        }
        total++; // newline
    }

    char *result = malloc(total + 1);
    if (!result) return NULL;

    char *pos = result;
    for (int32_t i = 0; i < s->height; i++) {
        for (int32_t j = 0; j < s->rows[i].count; j++) {
            if (s->rows[i].cells[j].data) {
                size_t len = strlen(s->rows[i].cells[j].data);
                memcpy(pos, s->rows[i].cells[j].data, len);
                pos += len;
            }
        }
        *pos++ = '\n';
    }
    *pos = '\0';

    return result;
}

//! Create a sketch from a list of rows
static TexSketch *sketch_from_rows(TexRow *rows, int32_t height, int32_t horizon) {
    TexSketch *s = calloc(1, sizeof(TexSketch));
    if (!s) return NULL;

    s->height = height;
    s->horizon = horizon;
    s->rows = rows;

    int32_t max_w = 0;
    for (int32_t i = 0; i < height; i++) {
        if (rows[i].count > max_w) max_w = rows[i].count;
    }
    s->width = max_w;

    return s;
}

//! Clone a sketch
static TexSketch *sketch_clone(const TexSketch *src) {
    if (!src) return NULL;

    TexSketch *s = calloc(1, sizeof(TexSketch));
    s->height = src->height;
    s->width = src->width;
    s->horizon = src->horizon;
    s->rows = malloc(src->height * sizeof(TexRow));

    for (int32_t i = 0; i < src->height; i++) {
        row_init(&s->rows[i]);
        for (int32_t j = 0; j < src->rows[i].count; j++) {
            row_push(&s->rows[i], src->rows[i].cells[j].data);
        }
    }

    return s;
}

//! Create an empty sketch (one row with no cells)
static TexSketch *sketch_empty(void) {
    TexSketch *s = calloc(1, sizeof(TexSketch));
    s->height = 1;
    s->width = 0;
    s->horizon = 0;
    s->rows = malloc(sizeof(TexRow));
    row_init(&s->rows[0]);
    return s;
}

//! Check if sketch is empty (no cells)
static bool sketch_is_empty(const TexSketch *s) {
    if (!s || s->height == 0) return true;
    if (s->height == 1 && s->rows[0].count == 0) return true;
    return false;
}

// #endregion

// #region Lexer

static const char *SPECIAL_CHARS = " ^_{}[]~";
static const char *SYMBOL_CHARS = "`!@#$%&*()+-=|;:'\",.<>/?";

static TexTokenType get_char_type(char c) {
    if (ISALPHA_(c)) return TEX_TOK_ALPH;
    if (ISDIGIT_(c)) return TEX_TOK_NUMB;
    if (strchr(SPECIAL_CHARS, c) || strchr(SYMBOL_CHARS, c)) return TEX_TOK_SYMB;
    return TEX_TOK_SYMB;
}

TexToken *tex_lex(const char *input, size_t len, int32_t *out_count) {
    char *tex = malloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        tex[i] = (input[i] == '\n' || input[i] == '\r' || input[i] == '\t') ? ' ' : input[i];
    }
    tex[len] = '\0';

    int32_t capacity = TEX_INITIAL_TOKEN_CAPACITY;
    TexToken *tokens = calloc(capacity, sizeof(TexToken));
    int32_t count = 0;

    char token_val[TEX_MAX_TOKEN_LEN];
    int32_t val_len = 0;
    TexTokenType token_type = TEX_TOK_NONE;
    TexToken prev_token = {0};

    for (size_t i = 0; i < len; i++) {
        char c = tex[i];
        TexTokenType char_type = get_char_type(c);

        if (val_len < TEX_MAX_TOKEN_LEN - 1) {
            token_val[val_len++] = c;
            token_val[val_len] = '\0';
        }

        bool is_final_char = (i == len - 1);

        // Handle backslash commands
        if (val_len > 1 && token_val[0] == '\\') {
            if (!is_final_char &&
                char_type == get_char_type(tex[i + 1]) &&
                char_type != TEX_TOK_SYMB) {
                continue;
            } else {
                // Remove leading backslash
                memmove(token_val, token_val + 1, val_len);
                val_len--;
            }
        } else if (val_len == 1 && token_val[0] == '\\') {
            token_type = TEX_TOK_CMND;
            if (is_final_char) {
                // Error: unexpected end
                val_len = 0;
            }
            continue;
        } else if (val_len == 1 && token_val[0] == '$') {
            token_type = TEX_TOK_SYMB;
            if (!is_final_char && tex[i + 1] == '$') {
                continue;
            }
        } else {
            token_type = char_type;
        }

        // Skip consecutive spaces at start or after space
        if (token_type == TEX_TOK_SYMB && val_len == 1 && token_val[0] == ' ') {
            if (prev_token.type == TEX_TOK_NONE ||
                (prev_token.type == TEX_TOK_SYMB && strcmp(prev_token.value, " ") == 0)) {
                val_len = 0;
                token_type = TEX_TOK_NONE;
                continue;
            }
        }

        // Store token
        if (count >= capacity) {
            capacity *= 2;
            tokens = realloc(tokens, capacity * sizeof(TexToken));
        }

        tokens[count].type = token_type;
        strcpy(tokens[count].value, token_val);
        tokens[count].value_len = val_len;
        prev_token = tokens[count];
        count++;

        val_len = 0;
        token_type = TEX_TOK_NONE;
    }

    free(tex);

    if (count == 0) {
        *out_count = 0;
        return tokens;
    }

    // Check if we need line wrapper
    bool need_line_wrapper = true;
    if (count > 0) {
        if (tokens[0].type == TEX_TOK_CMND &&
            (strcmp(tokens[0].value, "[") == 0 || strcmp(tokens[0].value, "(") == 0)) {
            need_line_wrapper = false;
        }
        if (tokens[0].type == TEX_TOK_SYMB &&
            (strcmp(tokens[0].value, "$") == 0 || strcmp(tokens[0].value, "$$") == 0)) {
            need_line_wrapper = false;
        }
        if (tokens[0].type == TEX_TOK_CMND && strcmp(tokens[0].value, "begin") == 0) {
            need_line_wrapper = false;
        }
    }

    // Add meta tokens
    int32_t meta_count = 2 + (need_line_wrapper ? 2 : 0);
    int32_t new_count = count + meta_count;
    tokens = realloc(tokens, new_count * sizeof(TexToken));

    // Shift existing tokens
    int32_t shift = 1 + (need_line_wrapper ? 1 : 0);
    memmove(tokens + shift, tokens, count * sizeof(TexToken));

    // Add start
    int32_t idx = 0;
    tokens[idx].type = TEX_TOK_META;
    strcpy(tokens[idx].value, "start");
    tokens[idx].value_len = 5;
    idx++;

    // Add startline if needed
    if (need_line_wrapper) {
        tokens[idx].type = TEX_TOK_META;
        strcpy(tokens[idx].value, "startline");
        tokens[idx].value_len = 9;
        idx++;
    }

    idx = shift + count;

    // Add endline if needed
    if (need_line_wrapper) {
        tokens[idx].type = TEX_TOK_META;
        strcpy(tokens[idx].value, "endline");
        tokens[idx].value_len = 7;
        idx++;
    }

    // Add end
    tokens[idx].type = TEX_TOK_META;
    strcpy(tokens[idx].value, "end");
    tokens[idx].value_len = 3;

    *out_count = new_count;
    return tokens;
}

// #endregion

// #region Parser - Type Lookup

//! Get type from token directly
static TexNodeType get_type_from_token(const TexToken *token) {
    switch (token->type) {
        case TEX_TOK_META:
            if (strcmp(token->value, "start") == 0) return TEX_NT_OPN_ROOT;
            if (strcmp(token->value, "end") == 0) return TEX_NT_CLS_ROOT;
            if (strcmp(token->value, "startline") == 0) return TEX_NT_OPN_LINE;
            if (strcmp(token->value, "endline") == 0) return TEX_NT_CLS_LINE;
            break;
        case TEX_TOK_SYMB:
            switch (token->value[0]) {
                case '^': return TEX_NT_SUP_SCRPT;
                case '_': return TEX_NT_SUB_SCRPT;
                case '{': return TEX_NT_OPN_BRAC;
                case '}': return TEX_NT_CLS_BRAC;
                case ' ': return TEX_NT_TXT_INVS;
                case '$': return (token->value[1] == '$') ? TEX_NT_OPN_DDLR : TEX_NT_OPN_DLLR;
            }
            break;
        case TEX_TOK_CMND:
            return tex_lookup_cmd_type(token->value);
        default:
            break;
    }
    return TEX_NT_NONE;
}

//! Main node type lookup
static TexNodeType get_node_type(const TexToken *token, TexNodeType parent_type) {
    TexNodeType nt;

    // Parent-dependent check
    nt = tex_get_parent_dep_type(parent_type, token->type, token->value);
    if (nt != TEX_NT_NONE) return nt;

    // Type-dependent: OPN_ENVN accepts any token as TXT_INFO
    if (parent_type == TEX_NT_OPN_ENVN &&
        (token->type == TEX_TOK_SYMB || token->type == TEX_TOK_ALPH || token->type == TEX_TOK_NUMB)) {
        return TEX_NT_TXT_INFO;
    }

    // Direct token lookup
    nt = get_type_from_token(token);
    if (nt != TEX_NT_NONE) return nt;

    // Self-dependent fallback
    if (token->type == TEX_TOK_CMND) return TEX_NT_CMD_LEAF;
    if (token->type == TEX_TOK_SYMB || token->type == TEX_TOK_ALPH || token->type == TEX_TOK_NUMB) {
        return TEX_NT_TXT_LEAF;
    }
    return TEX_NT_NONE;
}

// #endregion

// #region Parser - Node Type Info

//! Type info flags packed into a single byte
#define TI_POP_IN    0x01  //! Pop only if in list (else pop if NOT in list)
#define TI_ADD_NODE  0x02  //! Can add to nodes tree
#define TI_CHILD     0x04  //! Can be added to children list
#define TI_BREAK     0x08  //! Can break parent
#define TI_DBL_POP   0x10  //! Double pop behavior

typedef struct {
    TexNodeType type;
    uint8_t flags;
    uint8_t add_amount;
    TexNodeType pop_types[8];
    TexNodeType under_types[8];
} TexTypeInfoEntry;

//! Static type info table - indexed by node type enum
static const TexTypeInfoEntry TYPE_INFO[] = {
    // Container openers: pop_in, add_node, child, pop_types[], under_types[]
    [TEX_NT_OPN_ROOT]  = { TEX_NT_OPN_ROOT,  TI_POP_IN|TI_ADD_NODE,           1, {TEX_NT_CLS_ROOT}, {0} },
    [TEX_NT_OPN_BRAC]  = { TEX_NT_OPN_BRAC,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CLS_BRAC}, {0} },
    [TEX_NT_OPN_DEGR]  = { TEX_NT_OPN_DEGR,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CLS_DEGR}, {TEX_NT_CMD_SQRT} },
    [TEX_NT_OPN_DLIM]  = { TEX_NT_OPN_DLIM,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CLS_DLIM}, {0} },
    [TEX_NT_OPN_LINE]  = { TEX_NT_OPN_LINE,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CLS_LINE, TEX_NT_CMD_LBRK}, {TEX_NT_OPN_ROOT} },
    [TEX_NT_OPN_BRAK]  = { TEX_NT_OPN_BRAK,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CLS_BRAK, TEX_NT_CMD_LBRK}, {TEX_NT_OPN_ROOT} },
    [TEX_NT_OPN_PREN]  = { TEX_NT_OPN_PREN,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CLS_PREN, TEX_NT_CMD_LBRK}, {TEX_NT_OPN_ROOT} },
    [TEX_NT_OPN_DLLR]  = { TEX_NT_OPN_DLLR,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CLS_DLLR, TEX_NT_CMD_LBRK}, {TEX_NT_OPN_ROOT} },
    [TEX_NT_OPN_DDLR]  = { TEX_NT_OPN_DDLR,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CLS_DDLR, TEX_NT_CMD_LBRK}, {TEX_NT_OPN_ROOT} },
    [TEX_NT_OPN_ENVN]  = { TEX_NT_OPN_ENVN,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CLS_ENVN}, {TEX_NT_CMD_BGIN, TEX_NT_CMD_END} },
    [TEX_NT_OPN_TEXT]  = { TEX_NT_OPN_TEXT,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CLS_TEXT}, {TEX_NT_CMD_TEXT} },
    [TEX_NT_OPN_STKLN] = { TEX_NT_OPN_STKLN, TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CLS_STKLN, TEX_NT_STK_LBRK}, {TEX_NT_CMD_SBSTK} },

    // Commands
    [TEX_NT_CMD_SQRT]  = { TEX_NT_CMD_SQRT,  TI_ADD_NODE|TI_CHILD,            1, {TEX_NT_OPN_DEGR}, {0} },
    [TEX_NT_CMD_FRAC]  = { TEX_NT_CMD_FRAC,  TI_ADD_NODE|TI_CHILD,            2, {0}, {0} },
    [TEX_NT_CMD_BINOM] = { TEX_NT_CMD_BINOM, TI_ADD_NODE|TI_CHILD,            2, {0}, {0} },
    [TEX_NT_CMD_TEXT]  = { TEX_NT_CMD_TEXT,  TI_ADD_NODE|TI_CHILD,            1, {0}, {0} },
    [TEX_NT_CMD_SBSTK] = { TEX_NT_CMD_SBSTK, TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CLS_STKLN}, {0} },
    [TEX_NT_CMD_BGIN]  = { TEX_NT_CMD_BGIN,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  1, {TEX_NT_CMD_END, TEX_NT_CMD_LBRK}, {TEX_NT_OPN_ROOT} },
    [TEX_NT_CMD_END]   = { TEX_NT_CMD_END,   TI_ADD_NODE|TI_BREAK,            1, {0}, {TEX_NT_CMD_BGIN, TEX_NT_CMD_LBRK} },
    [TEX_NT_CMD_LBRK]  = { TEX_NT_CMD_LBRK,  TI_POP_IN|TI_ADD_NODE|TI_CHILD|TI_BREAK, 1,
                          {TEX_NT_CMD_LBRK, TEX_NT_CLS_LINE, TEX_NT_CLS_BRAK, TEX_NT_CLS_PREN, TEX_NT_CLS_DLLR, TEX_NT_CLS_DDLR, TEX_NT_CMD_END},
                          {TEX_NT_CMD_LBRK, TEX_NT_OPN_LINE, TEX_NT_OPN_BRAK, TEX_NT_OPN_PREN, TEX_NT_OPN_DLLR, TEX_NT_OPN_DDLR, TEX_NT_CMD_BGIN} },
    [TEX_NT_CMD_ACNT]  = { TEX_NT_CMD_ACNT,  TI_ADD_NODE|TI_CHILD,            1, {0}, {0} },
    [TEX_NT_CMD_FONT]  = { TEX_NT_CMD_FONT,  TI_ADD_NODE|TI_CHILD,            1, {0}, {0} },
    [TEX_NT_CMD_LMTS]  = { TEX_NT_CMD_LMTS,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  0, {0}, {0} },
    [TEX_NT_CMD_STYL]  = { TEX_NT_CMD_STYL,  TI_POP_IN,                       0, {0}, {0} },

    // Scripts
    [TEX_NT_SUP_SCRPT] = { TEX_NT_SUP_SCRPT, TI_ADD_NODE|TI_CHILD,            1, {0}, {0} },
    [TEX_NT_SUB_SCRPT] = { TEX_NT_SUB_SCRPT, TI_ADD_NODE|TI_CHILD,            1, {0}, {0} },
    [TEX_NT_TOP_SCRPT] = { TEX_NT_TOP_SCRPT, TI_ADD_NODE|TI_CHILD,            1, {0}, {0} },
    [TEX_NT_BTM_SCRPT] = { TEX_NT_BTM_SCRPT, TI_ADD_NODE|TI_CHILD,            1, {0}, {0} },

    // Delimiters
    [TEX_NT_BIG_DLIM]  = { TEX_NT_BIG_DLIM,  TI_ADD_NODE|TI_CHILD,            1, {0}, {0} },
    [TEX_NT_CLS_DLIM]  = { TEX_NT_CLS_DLIM,  TI_ADD_NODE|TI_CHILD,            1, {0}, {0} },

    // Leaves
    [TEX_NT_TXT_LEAF]  = { TEX_NT_TXT_LEAF,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  0, {0}, {0} },
    [TEX_NT_TXT_INFO]  = { TEX_NT_TXT_INFO,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  0, {0}, {0} },
    [TEX_NT_TXT_INVS]  = { TEX_NT_TXT_INVS,  TI_POP_IN,                       0, {0}, {0} },
    [TEX_NT_CMD_LEAF]  = { TEX_NT_CMD_LEAF,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  0, {0}, {0} },
    [TEX_NT_CTR_BASE]  = { TEX_NT_CTR_BASE,  TI_POP_IN|TI_ADD_NODE|TI_CHILD,  0, {0}, {0} },

    // Closers
    [TEX_NT_CLS_ROOT]  = { TEX_NT_CLS_ROOT,  TI_POP_IN,                       0, {0}, {TEX_NT_OPN_ROOT} },
    [TEX_NT_CLS_BRAC]  = { TEX_NT_CLS_BRAC,  TI_POP_IN,                       0, {0}, {TEX_NT_OPN_BRAC} },
    [TEX_NT_CLS_DEGR]  = { TEX_NT_CLS_DEGR,  TI_POP_IN,                       0, {0}, {TEX_NT_OPN_DEGR} },
    [TEX_NT_CLS_LINE]  = { TEX_NT_CLS_LINE,  TI_POP_IN,                       0, {0}, {TEX_NT_OPN_LINE, TEX_NT_CMD_LBRK} },
    [TEX_NT_CLS_BRAK]  = { TEX_NT_CLS_BRAK,  TI_POP_IN,                       0, {0}, {TEX_NT_OPN_BRAK, TEX_NT_CMD_LBRK} },
    [TEX_NT_CLS_PREN]  = { TEX_NT_CLS_PREN,  TI_POP_IN,                       0, {0}, {TEX_NT_OPN_PREN, TEX_NT_CMD_LBRK} },
    [TEX_NT_CLS_DLLR]  = { TEX_NT_CLS_DLLR,  TI_POP_IN,                       0, {0}, {TEX_NT_OPN_DLLR, TEX_NT_CMD_LBRK} },
    [TEX_NT_CLS_DDLR]  = { TEX_NT_CLS_DDLR,  TI_POP_IN|TI_BREAK,              0, {0}, {TEX_NT_OPN_DDLR, TEX_NT_CMD_LBRK} },
    [TEX_NT_CLS_ENVN]  = { TEX_NT_CLS_ENVN,  TI_POP_IN,                       0, {0}, {TEX_NT_OPN_ENVN} },
    [TEX_NT_CLS_TEXT]  = { TEX_NT_CLS_TEXT,  TI_POP_IN,                       0, {0}, {TEX_NT_OPN_TEXT} },
    [TEX_NT_CLS_STKLN] = { TEX_NT_CLS_STKLN, TI_POP_IN|TI_BREAK|TI_DBL_POP,   0, {0}, {TEX_NT_OPN_STKLN, TEX_NT_STK_LBRK} },

    // Special
    [TEX_NT_STK_LBRK]  = { TEX_NT_STK_LBRK,  TI_POP_IN|TI_ADD_NODE|TI_CHILD|TI_BREAK, 1, {TEX_NT_CLS_STKLN, TEX_NT_STK_LBRK}, {TEX_NT_OPN_STKLN, TEX_NT_STK_LBRK} },
};

//! Check if type is in array
static inline bool type_in_array(TexNodeType t, const TexNodeType *arr) {
    for (int32_t i = 0; i < 8 && arr[i] != TEX_NT_NONE; i++) {
        if (arr[i] == t) return true;
    }
    return false;
}

//! Check if node type can pop parent
static bool can_pop(TexNodeType parent_type, TexNodeType node_type) {
    if (parent_type == TEX_NT_NONE) return false;
    const TexTypeInfoEntry *info = &TYPE_INFO[parent_type];
    bool found = type_in_array(node_type, info->pop_types);
    return (info->flags & TI_POP_IN) ? found : !found;
}

//! Check if type is a script type
static inline bool is_script_type(TexNodeType t) {
    return t == TEX_NT_SUP_SCRPT || t == TEX_NT_SUB_SCRPT ||
           t == TEX_NT_TOP_SCRPT || t == TEX_NT_BTM_SCRPT;
}

//! Get script base node (for attaching scripts)
static int32_t get_script_base(TexNodeType node_type, TexNodeArray *nodes, int32_t *parent_stack, int32_t stack_count) {
    if (!is_script_type(node_type) || stack_count == 0) return -1;

    int32_t parent_id = parent_stack[stack_count - 1];
    TexIdArray *siblings = &nodes->nodes[parent_id].children;
    if (siblings->count == 0) return -1;

    int32_t base_id = siblings->data[siblings->count - 1];
    if (is_script_type(nodes->nodes[base_id].type)) {
        return (siblings->count >= 2) ? siblings->data[siblings->count - 2] : -1;
    }
    return base_id;
}

//! Update script type based on base type (center base -> top/bottom scripts)
static inline TexNodeType update_script_type(TexNodeType base_type, TexNodeType script_type) {
    if (base_type != TEX_NT_CTR_BASE) return script_type;
    if (script_type == TEX_NT_SUP_SCRPT) return TEX_NT_TOP_SCRPT;
    if (script_type == TEX_NT_SUB_SCRPT) return TEX_NT_BTM_SCRPT;
    return script_type;
}

TexNodeArray *tex_parse(TexToken *tokens, int32_t count) {
    TexNodeArray *arr = calloc(1, sizeof(TexNodeArray));
    arr->nodes = calloc(TEX_INITIAL_NODE_CAPACITY, sizeof(TexNode));
    arr->capacity = TEX_INITIAL_NODE_CAPACITY;
    arr->count = 0;

    int32_t *parent_stack = calloc(TEX_INITIAL_STACK_CAPACITY, sizeof(int32_t));
    int32_t stack_count = 0;
    int32_t stack_capacity = TEX_INITIAL_STACK_CAPACITY;

    for (int32_t i = 0; i < count; i++) {
        TexToken *token = &tokens[i];

        TexNodeType parent_type = TEX_NT_NONE;
        int32_t parent_id = -1;
        if (stack_count > 0) {
            parent_id = parent_stack[stack_count - 1];
            parent_type = arr->nodes[parent_id].type;
        }

        TexNodeType node_type = get_node_type(token, parent_type);

        // Skip invisible spaces
        if (node_type == TEX_NT_TXT_INVS) continue;

        const TexTypeInfoEntry *info = &TYPE_INFO[node_type];

        bool can_add_to_children = (info->flags & TI_CHILD);
        bool can_pop_parent = can_pop(parent_type, node_type);
        bool can_update_parent = (info->flags & TI_BREAK);
        bool can_dbl_pop = (info->flags & TI_DBL_POP);

        int32_t base_id = get_script_base(node_type, arr, parent_stack, stack_count);

        if (base_id != -1) {
            TexNodeType base_type = arr->nodes[base_id].type;
            node_type = update_script_type(base_type, node_type);
            id_array_push(&arr->nodes[base_id].scripts, arr->count);
            can_add_to_children = false;
            can_pop_parent = false;
        }

        if (can_pop_parent && stack_count > 0) {
            stack_count--;
        }

        if (can_update_parent && stack_count > 0) {
            parent_id = parent_stack[stack_count - 1];
            parent_type = arr->nodes[parent_id].type;
        }

        if (can_dbl_pop && stack_count > 0) {
            stack_count--;
            if (stack_count > 0) {
                parent_id = parent_stack[stack_count - 1];
                parent_type = arr->nodes[parent_id].type;
            }
        }

        if (can_add_to_children && parent_id >= 0) {
            id_array_push(&arr->nodes[parent_id].children, arr->count);
        }

        // Add to parent stack
        for (int32_t j = 0; j < info->add_amount; j++) {
            if (stack_count >= stack_capacity) {
                stack_capacity *= 2;
                parent_stack = realloc(parent_stack, stack_capacity * sizeof(int32_t));
            }
            parent_stack[stack_count++] = arr->count;
        }

        // Add node
        if (info->flags & TI_ADD_NODE) {
            if (arr->count >= arr->capacity) {
                arr->capacity *= 2;
                arr->nodes = realloc(arr->nodes, arr->capacity * sizeof(TexNode));
            }
            TexNode *node = &arr->nodes[arr->count];
            node->type = node_type;
            node->token = *token;
            id_array_init(&node->children);
            id_array_init(&node->scripts);
            arr->count++;
        }
    }

    free(parent_stack);
    return arr;
}

void tex_nodes_free(TexNodeArray *arr) {
    if (!arr) return;
    for (int32_t i = 0; i < arr->count; i++) {
        id_array_free(&arr->nodes[i].children);
        id_array_free(&arr->nodes[i].scripts);
    }
    free(arr->nodes);
    free(arr);
}

// #endregion

// #region Renderer - Utility Functions

//! Get nth UTF-8 character from string
static const char *utf8_get_char(const char *s, int32_t idx) {
    static char buf[8];
    if (!s) return NULL;

    const uint8_t *p = (const uint8_t *)s;
    int32_t i = 0;
    while (*p && i < idx) {
        int32_t len = utf8proc_utf8class[*p];
        if (len < 1) len = 1;
        p += len;
        i++;
    }

    if (!*p) return NULL;

    int32_t len = utf8proc_utf8class[*p];
    if (len < 1) len = 1;
    memcpy(buf, p, len);
    buf[len] = '\0';
    return buf;
}

//! Get character at index in alphabet string
static const char *alphabet_char_at(const char *alphabet, int32_t idx) {
    return utf8_get_char(alphabet, idx);
}

//! Apply font to a character
static const char *apply_font(const char *ch, TexFontStyle style) {
    if (!ch) return TEX_BG;

    char c = ch[0];
    if (!ISALPHA_(c)) return ch;

    const char *alphabet = tex_get_alphabet(style);
    int32_t idx = ISUPPER_(c) ? (c - 'A') : (c - 'a' + 26);

    const char *result = alphabet_char_at(alphabet, idx);
    return result ? result : ch;
}

//! Revert font styling and apply new font
static TexSketch *util_font(const char *font_val, TexSketch *child) {
    if (!child) return sketch_empty();

    TexFontStyle style = tex_get_font_style(font_val);
    TexSketch *result = sketch_clone(child);

    for (int32_t i = 0; i < result->height; i++) {
        TexRow *row = &result->rows[i];
        for (int32_t j = 0; j < row->count; j++) {
            char *old_ch = row->cells[j].data;
            if (!old_ch) continue;

            // Revert to ASCII first
            char reverted = tex_revert_font_char(old_ch);
            if (reverted && ISALPHA_(reverted)) {
                // Apply new font
                char temp[2] = {reverted, '\0'};
                const char *styled = apply_font(temp, style);
                free(row->cells[j].data);
                row->cells[j].data = tex_strdup(styled);
            }
        }
    }

    return result;
}

//! Concatenate sketches horizontally with horizon alignment
static TexSketch *util_concat(TexSketch **children, int32_t child_count, bool concat_line, bool align_amp) {
    if (child_count == 0) return sketch_empty();

    // Find max heights above and below horizon
    int32_t maxh_sky = 0;
    int32_t maxh_ocn = 0;
    bool contain_amp = false;

    for (int32_t i = 0; i < child_count; i++) {
        if (!children[i]) continue;
        int32_t horizon = children[i]->horizon;
        if (horizon == -1) {
            contain_amp = true;
            if (!align_amp) continue;
            continue;
        }
        int32_t h_sky = horizon;
        int32_t h_ocn = children[i]->height - horizon - 1;
        if (h_sky > maxh_sky) maxh_sky = h_sky;
        if (h_ocn > maxh_ocn) maxh_ocn = h_ocn;
    }

    int32_t new_height = maxh_sky + 1 + maxh_ocn;
    int32_t concated_horizon = maxh_sky;

    // Create result rows
    TexRow *rows = calloc(new_height, sizeof(TexRow));
    for (int32_t i = 0; i < new_height; i++) {
        row_init(&rows[i]);
    }

    // Concatenate each child
    for (int32_t c = 0; c < child_count; c++) {
        TexSketch *child = children[c];
        if (!child) continue;

        int32_t horizon = child->horizon;
        if (horizon == -1) {
            if (align_amp) {
                concated_horizon = rows[0].count;
            }
            continue;
        }

        int32_t h_sky = horizon;
        int32_t h_ocn = child->height - horizon - 1;
        int32_t top_pad_len = maxh_sky - h_sky;
        int32_t btm_pad_len = maxh_ocn - h_ocn;

        int32_t child_width = child->width;
        if (child->height > 0 && child->rows[0].count > child_width) {
            child_width = child->rows[0].count;
        }

        // Add top padding
        for (int32_t r = 0; r < top_pad_len; r++) {
            for (int32_t j = 0; j < child_width; j++) {
                row_push(&rows[r], TEX_BG);
            }
        }

        // Add child rows
        for (int32_t r = 0; r < child->height; r++) {
            int32_t dest_row = top_pad_len + r;
            for (int32_t j = 0; j < child->rows[r].count; j++) {
                row_push(&rows[dest_row], child->rows[r].cells[j].data);
            }
            // Pad to child_width
            for (int32_t j = child->rows[r].count; j < child_width; j++) {
                row_push(&rows[dest_row], TEX_BG);
            }
        }

        // Add bottom padding
        for (int32_t r = 0; r < btm_pad_len; r++) {
            int32_t dest_row = top_pad_len + child->height + r;
            for (int32_t j = 0; j < child_width; j++) {
                row_push(&rows[dest_row], TEX_BG);
            }
        }
    }

    if (concat_line && !contain_amp) {
        concated_horizon = rows[0].count;
    }

    return sketch_from_rows(rows, new_height, concated_horizon);
}

//! Stack sketches vertically with alignment
static TexSketch *util_vert_pile(TexSketch *top, TexSketch *ctr, int32_t ctr_horizon, TexSketch *btm, TexAlign align) {
    int32_t top_height = (top && !sketch_is_empty(top)) ? top->height : 0;
    int32_t ctr_height = (ctr && !sketch_is_empty(ctr)) ? ctr->height : 0;
    int32_t btm_height = (btm && !sketch_is_empty(btm)) ? btm->height : 0;

    int32_t piled_horizon = top_height + ctr_horizon;
    if (top && sketch_is_empty(top)) piled_horizon--;
    if (ctr && sketch_is_empty(ctr)) piled_horizon--;
    if (piled_horizon < 0) piled_horizon = 0;

    int32_t max_len = 0;
    if (top && !sketch_is_empty(top) && top->rows[0].count > max_len) max_len = top->rows[0].count;
    if (ctr && !sketch_is_empty(ctr) && ctr->rows[0].count > max_len) max_len = ctr->rows[0].count;
    if (btm && !sketch_is_empty(btm) && btm->rows[0].count > max_len) max_len = btm->rows[0].count;

    int32_t total_height = top_height + ctr_height + btm_height;
    if (total_height == 0) return sketch_empty();

    TexRow *rows = calloc(total_height, sizeof(TexRow));
    int32_t row_idx = 0;

    TexSketch *parts[] = {top, ctr, btm};
    for (int32_t p = 0; p < 3; p++) {
        TexSketch *part = parts[p];
        if (!part || sketch_is_empty(part)) continue;

        int32_t part_len = part->rows[0].count;
        int32_t left_pad = 0;

        if (align == TEX_ALIGN_CENTER) {
            left_pad = (max_len - part_len) / 2;
        } else if (align == TEX_ALIGN_RIGHT) {
            left_pad = max_len - part_len;
        }

        for (int32_t r = 0; r < part->height; r++) {
            row_init(&rows[row_idx]);

            // Left padding
            for (int32_t j = 0; j < left_pad; j++) {
                row_push(&rows[row_idx], TEX_BG);
            }

            // Content
            for (int32_t j = 0; j < part->rows[r].count; j++) {
                row_push(&rows[row_idx], part->rows[r].cells[j].data);
            }

            // Right padding
            int32_t right_pad = max_len - left_pad - part->rows[r].count;
            for (int32_t j = 0; j < right_pad; j++) {
                row_push(&rows[row_idx], TEX_BG);
            }

            row_idx++;
        }
    }

    return sketch_from_rows(rows, total_height, piled_horizon);
}

//! Try to shrink sketch to script characters
//! @param script_type_id 0 = superscript, 1 = subscript
//! @param smart Allow keeping existing opposite script
//! @param switch_script Convert opposite script to target script
static TexSketch *util_shrink(TexSketch *sketch, int32_t script_type_id, bool smart, bool switch_script) {
    if (!sketch || sketch->height != 1) return NULL;

    TexRow *row = &sketch->rows[0];
    TexRow new_row;
    row_init(&new_row);

    for (int32_t i = 0; i < row->count; i++) {
        const char *ch = row->cells[i].data;
        if (!ch) {
            row_free(&new_row);
            return NULL;
        }

        // Revert font styling
        char reverted = tex_revert_font_char(ch);
        const char *unshrunk = tex_unshrink_char(ch);

        // Get the base character
        char base_char = reverted ? reverted : (unshrunk ? unshrunk[0] : ch[0]);
        if (!base_char) base_char = ch[0];

        char base_str[8];
        base_str[0] = base_char;
        base_str[1] = '\0';

        // Check if already in target script
        const char *target_script = (script_type_id == 0) ?
            tex_to_superscript(base_str) : tex_to_subscript(base_str);
        const char *other_script = (script_type_id == 0) ?
            tex_to_subscript(base_str) : tex_to_superscript(base_str);

        // Check if current char is already in a script
        if (target_script && strcmp(ch, target_script) == 0) {
            // Already in target script - that means we can't shrink more
            row_free(&new_row);
            return NULL;
        }

        if (other_script && strcmp(ch, other_script) == 0) {
            // Already in opposite script
            if (smart) {
                row_push(&new_row, ch);
                continue;
            }
            if (switch_script && target_script) {
                row_push(&new_row, target_script);
                continue;
            }
            row_free(&new_row);
            return NULL;
        }

        // Try to convert to target script
        if (target_script && strcmp(target_script, " ") != 0) {
            row_push(&new_row, target_script);
        } else if (base_char == ' ') {
            row_push(&new_row, " ");
        } else {
            row_free(&new_row);
            return NULL;
        }
    }

    TexRow *result_rows = malloc(sizeof(TexRow));
    *result_rows = new_row;
    return sketch_from_rows(result_rows, 1, 0);
}

//! Render script (super or sub)
static TexSketch *util_script(TexSketch *child, int32_t script_type_id) {
    if (!child) return sketch_empty();

    // Try to shrink
    TexSketch *shrunk = util_shrink(child, script_type_id, false, false);
    if (shrunk) return shrunk;

    // Try smart shrink with opposite script
    TexSketch *smart_shrunk = util_shrink(child, 1 - script_type_id, true, false);
    TexSketch *use_sketch = smart_shrunk ? smart_shrunk : child;

    TexSketch *empty = sketch_empty();
    TexSketch *result;

    if (script_type_id == 0) {
        // Superscript: top = child, bottom = empty
        TexSketch *bg = sketch_empty();
        row_push(&bg->rows[0], TEX_BG);
        result = util_vert_pile(use_sketch, bg, 0, empty, TEX_ALIGN_LEFT);
        tex_sketch_free(bg);
    } else {
        // Subscript: top = empty, bottom = child
        TexSketch *bg = sketch_empty();
        row_push(&bg->rows[0], TEX_BG);
        result = util_vert_pile(empty, bg, 0, use_sketch, TEX_ALIGN_LEFT);
        tex_sketch_free(bg);
    }

    tex_sketch_free(empty);
    if (smart_shrunk) tex_sketch_free(smart_shrunk);

    return result;
}

//! Get pile center based on base height
static TexSketch *util_get_pile_center(int32_t base_height, int32_t base_horizon, int32_t *out_horizon) {
    if (base_height == 2) {
        *out_horizon = (base_horizon == 0) ? 0 : 1;
        return sketch_empty();
    }
    if (base_height == 1) {
        *out_horizon = 0;
        return sketch_empty();
    }

    // Create center with (base_height - 2) rows
    int32_t center_height = base_height - 2;
    TexRow *rows = calloc(center_height, sizeof(TexRow));
    for (int32_t i = 0; i < center_height; i++) {
        row_init(&rows[i]);
        row_push(&rows[i], TEX_BG);
    }

    *out_horizon = base_horizon - 1;
    return sketch_from_rows(rows, center_height, *out_horizon);
}

//! Build delimiter sketch
static TexSketch *util_delimiter(const char *delim_type, int32_t height, int32_t horizon) {
    if (!delim_type || strcmp(delim_type, ".") == 0) {
        return sketch_empty();
    }

    char delim_char = delim_type[0];

    if (height == 1) {
        TexRow *row = malloc(sizeof(TexRow));
        row_init(row);
        row_push(row, delim_type);
        return sketch_from_rows(row, 1, 0);
    }

    // Handle { and } needing at least 3 height
    if (height == 2 && (delim_char == '{' || delim_char == '}')) {
        height = 3;
        if (horizon == 0) horizon = 1;
    }

    int32_t center = horizon;
    if (center == 0) center = 1;
    if (center == height - 1) center = height - 2;

    TexRow *rows = calloc(height, sizeof(TexRow));
    for (int32_t i = 0; i < height; i++) {
        row_init(&rows[i]);
        TexDelimPos pos = (i == 0) ? TEX_DELIM_TOP :
                          (i == height - 1) ? TEX_DELIM_BTM :
                          (i == center) ? TEX_DELIM_CTR : TEX_DELIM_FIL;
        const char *ch = tex_get_delimiter_char(delim_char, pos);
        row_push(&rows[i], ch ? ch : delim_type);
    }

    return sketch_from_rows(rows, height, horizon);
}

// #endregion

// #region Renderer - Node Rendering

static TexSketch *render_node(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode);

//! Render leaf node (symbol, number, letter)
static TexSketch *render_leaf(TexToken *token, bool use_serif) {
    TexRow *row = malloc(sizeof(TexRow));
    row_init(row);

    if (token->type == TEX_TOK_NUMB) {
        row_push(row, token->value);
        return sketch_from_rows(row, 1, 0);
    }

    if (token->type == TEX_TOK_SYMB) {
        if (strcmp(token->value, "&") == 0) {
            row_push(row, "&");
            TexSketch *s = sketch_from_rows(row, 1, -1);
            return s;
        }
        // Simple symbols pass through
        row_push(row, token->value);
        return sketch_from_rows(row, 1, 0);
    }

    if (token->type == TEX_TOK_ALPH) {
        // Apply math normal font
        TexFontStyle style = use_serif ? TEX_FONT_SERIF_IT : TEX_FONT_NORMAL;
        const char *styled = apply_font(token->value, style);
        row_push(row, styled);
        return sketch_from_rows(row, 1, 0);
    }

    if (token->type == TEX_TOK_CMND) {
        // Check for multi-line command
        int32_t op_height, op_width, op_horizon;
        const char *ml = tex_get_multiline_op(token->value, &op_height, &op_width, &op_horizon);
        if (ml) {
            TexRow *rows = calloc(op_height, sizeof(TexRow));
            const char *p = ml;
            for (int32_t r = 0; r < op_height; r++) {
                row_init(&rows[r]);
                for (int32_t c = 0; c < op_width; c++) {
                    int32_t len = utf8proc_utf8class[(uint8_t)*p];
                    if (len < 1) len = 1;
                    char buf[8];
                    memcpy(buf, p, len);
                    buf[len] = '\0';
                    row_push(&rows[r], buf);
                    p += len;
                }
            }
            row_free(row);
            free(row);
            return sketch_from_rows(rows, op_height, op_horizon);
        }

        // Lookup symbol
        const char *sym = tex_lookup_symbol(token->value);
        if (sym) {
            // Symbol may have multiple characters
            int32_t i = 0;
            while (sym[i]) {
                const char *ch = utf8_get_char(sym, i);
                if (ch) row_push(row, ch);
                i++;
                // Check if we've processed all chars
                const char *next = utf8_get_char(sym, i);
                if (!next) break;
            }
            if (row->count == 0) row_push(row, sym);
            return sketch_from_rows(row, 1, 0);
        }

        // Unknown - show as ?
        row_push(row, "?");
        return sketch_from_rows(row, 1, 0);
    }

    row_push(row, "?");
    return sketch_from_rows(row, 1, 0);
}

//! Render center base (sum, prod, lim)
static TexSketch *render_ctr_base(TexToken *token) {
    int32_t height, width, horizon;
    const char *ml = tex_get_multiline_op(token->value, &height, &width, &horizon);
    if (ml) {
        TexRow *rows = calloc(height, sizeof(TexRow));
        const char *p = ml;
        for (int32_t r = 0; r < height; r++) {
            row_init(&rows[r]);
            for (int32_t c = 0; c < width; c++) {
                int32_t len = utf8proc_utf8class[(uint8_t)*p];
                if (len < 1) len = 1;
                char buf[8];
                memcpy(buf, p, len);
                buf[len] = '\0';
                row_push(&rows[r], buf);
                p += len;
            }
        }
        return sketch_from_rows(rows, height, horizon);
    }

    // Fallback: lookup symbol and split into characters
    TexRow *row = malloc(sizeof(TexRow));
    row_init(row);
    const char *sym = tex_lookup_symbol(token->value);
    if (sym) {
        const char *p = sym;
        while (*p) {
            int32_t len = utf8proc_utf8class[(uint8_t)*p];
            if (len < 1) len = 1;
            char buf[8];
            memcpy(buf, p, len);
            buf[len] = '\0';
            row_push(row, buf);
            p += len;
        }
    } else {
        row_push(row, "?");
    }
    return sketch_from_rows(row, 1, 0);
}

//! Render children as concatenation
static TexSketch *render_concat(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    TexNode *node = &nodes->nodes[node_id];
    int32_t child_count = node->children.count;

    if (child_count == 0) return sketch_empty();

    TexSketch **children = calloc(child_count, sizeof(TexSketch *));
    for (int32_t i = 0; i < child_count; i++) {
        children[i] = render_node(nodes, node->children.data[i], use_serif, inline_mode);
    }

    TexSketch *result = util_concat(children, child_count, false, false);

    for (int32_t i = 0; i < child_count; i++) {
        tex_sketch_free(children[i]);
    }
    free(children);

    return result;
}

//! Apply scripts to base sketch
static TexSketch *render_apply_scripts(TexSketch *base, TexNodeArray *nodes, TexIdArray *script_ids, bool use_serif, bool inline_mode) {
    if (script_ids->count == 0) return base;

    TexSketch *top = NULL;
    TexSketch *btm = NULL;
    TexAlign base_position = TEX_ALIGN_LEFT;

    for (int32_t i = 0; i < script_ids->count; i++) {
        int32_t script_id = script_ids->data[i];
        TexNode *script_node = &nodes->nodes[script_id];
        TexSketch *script_sketch = render_node(nodes, script_id, use_serif, inline_mode);

        if (script_node->type == TEX_NT_TOP_SCRPT || script_node->type == TEX_NT_BTM_SCRPT) {
            base_position = TEX_ALIGN_CENTER;
        }

        if (script_node->type == TEX_NT_SUP_SCRPT || script_node->type == TEX_NT_TOP_SCRPT) {
            if (top) tex_sketch_free(top);
            top = script_sketch;
        } else if (script_node->type == TEX_NT_SUB_SCRPT || script_node->type == TEX_NT_BTM_SCRPT) {
            if (btm) tex_sketch_free(btm);
            btm = script_sketch;
        } else {
            tex_sketch_free(script_sketch);
        }
    }

    // Inline mode: use Unicode superscript/subscript characters for single-line output
    if (inline_mode) {
        TexRow *row = malloc(sizeof(TexRow));
        row_init(row);

        // Copy base characters (only first row for inline)
        if (base && base->height > 0) {
            for (int32_t c = 0; c < base->rows[0].count; c++) {
                if (base->rows[0].cells[c].data) {
                    row_push(row, base->rows[0].cells[c].data);
                }
            }
        }

        // Add superscript using util_shrink for Unicode conversion
        if (top && top->height > 0) {
            TexSketch *sup = util_shrink(top, 0, false, false);
            if (sup) {
                for (int32_t c = 0; c < sup->rows[0].count; c++) {
                    if (sup->rows[0].cells[c].data) {
                        row_push(row, sup->rows[0].cells[c].data);
                    }
                }
                tex_sketch_free(sup);
            } else {
                // Fallback: render as ^(content)
                row_push(row, "^");
                row_push(row, "(");
                for (int32_t c = 0; c < top->rows[0].count; c++) {
                    if (top->rows[0].cells[c].data) {
                        row_push(row, top->rows[0].cells[c].data);
                    }
                }
                row_push(row, ")");
            }
        }

        // Add subscript using util_shrink for Unicode conversion
        if (btm && btm->height > 0) {
            TexSketch *sub = util_shrink(btm, 1, false, false);
            if (sub) {
                for (int32_t c = 0; c < sub->rows[0].count; c++) {
                    if (sub->rows[0].cells[c].data) {
                        row_push(row, sub->rows[0].cells[c].data);
                    }
                }
                tex_sketch_free(sub);
            } else {
                // Fallback: render as _(content)
                row_push(row, "_");
                row_push(row, "(");
                for (int32_t c = 0; c < btm->rows[0].count; c++) {
                    if (btm->rows[0].cells[c].data) {
                        row_push(row, btm->rows[0].cells[c].data);
                    }
                }
                row_push(row, ")");
            }
        }

        tex_sketch_free(top);
        tex_sketch_free(btm);
        return sketch_from_rows(row, 1, 0);
    }

    if (base_position == TEX_ALIGN_CENTER) {
        TexSketch *result = util_vert_pile(top, base, base->horizon, btm, TEX_ALIGN_CENTER);
        tex_sketch_free(top);
        tex_sketch_free(btm);
        return result;
    }

    // Side scripts
    int32_t ctr_horizon;
    TexSketch *ctr = util_get_pile_center(base->height, base->horizon, &ctr_horizon);

    if (ctr && !sketch_is_empty(ctr)) {
        TexSketch *piled = util_vert_pile(top, ctr, ctr_horizon, btm, TEX_ALIGN_LEFT);
        TexSketch *parts[] = {base, piled};
        TexSketch *result = util_concat(parts, 2, false, false);
        tex_sketch_free(top);
        tex_sketch_free(btm);
        tex_sketch_free(ctr);
        tex_sketch_free(piled);
        return result;
    }
    tex_sketch_free(ctr);

    // Handle single-line base with scripts
    if (!top && btm) {
        TexSketch *parts[] = {base, btm};
        TexSketch *result = util_concat(parts, 2, false, false);
        tex_sketch_free(btm);
        return result;
    }
    if (top && !btm) {
        // Adjust horizon for top script
        int32_t top_height = top->height;
        TexRow *top_rows = malloc(top_height * sizeof(TexRow));
        for (int32_t i = 0; i < top_height; i++) {
            row_init(&top_rows[i]);
            for (int32_t j = 0; j < top->rows[i].count; j++) {
                row_push(&top_rows[i], top->rows[i].cells[j].data);
            }
        }
        TexSketch *top_adj = sketch_from_rows(top_rows, top_height, top_height - 1);
        TexSketch *parts[] = {base, top_adj};
        TexSketch *result = util_concat(parts, 2, false, false);
        tex_sketch_free(top);
        tex_sketch_free(top_adj);
        return result;
    }

    // Handle multi-row scripts
    TexSketch *ctr_new = NULL;
    int32_t ctr_horizon_new = 0;
    TexSketch *top_adj = top;
    TexSketch *btm_adj = btm;

    if (top && top->height > 1) {
        // Remove last row of top
        int32_t new_height = top->height - 1;
        TexRow *new_rows = calloc(new_height, sizeof(TexRow));
        for (int32_t i = 0; i < new_height; i++) {
            row_init(&new_rows[i]);
            for (int32_t j = 0; j < top->rows[i].count; j++) {
                row_push(&new_rows[i], top->rows[i].cells[j].data);
            }
        }
        top_adj = sketch_from_rows(new_rows, new_height, 0);
        ctr_new = sketch_empty();
        ctr_horizon_new = 1;
    } else if (btm && btm->height > 1) {
        // Remove first row of btm
        int32_t new_height = btm->height - 1;
        TexRow *new_rows = calloc(new_height, sizeof(TexRow));
        for (int32_t i = 0; i < new_height; i++) {
            row_init(&new_rows[i]);
            for (int32_t j = 0; j < btm->rows[i + 1].count; j++) {
                row_push(&new_rows[i], btm->rows[i + 1].cells[j].data);
            }
        }
        btm_adj = sketch_from_rows(new_rows, new_height, 0);
        ctr_new = sketch_empty();
        ctr_horizon_new = 0;
    } else if (top && btm && top->height == 1 && btm->height == 1) {
        // Try to shrink both
        TexSketch *top_shrunk = util_shrink(top, 1, false, true);
        TexSketch *btm_shrunk = util_shrink(btm, 0, false, true);

        if (top_shrunk && btm_shrunk) {
            ctr_new = sketch_empty();
            row_push(&ctr_new->rows[0], TEX_BG);
            top_adj = top_shrunk;
            btm_adj = btm_shrunk;
        } else {
            if (top_shrunk) tex_sketch_free(top_shrunk);
            if (btm_shrunk) tex_sketch_free(btm_shrunk);
            ctr_new = sketch_empty();
            row_push(&ctr_new->rows[0], TEX_BG);
        }
    } else {
        ctr_new = sketch_empty();
        row_push(&ctr_new->rows[0], TEX_BG);
    }

    TexSketch *piled = util_vert_pile(top_adj, ctr_new, ctr_horizon_new, btm_adj, TEX_ALIGN_LEFT);
    TexSketch *parts[] = {base, piled};
    TexSketch *result = util_concat(parts, 2, false, false);

    if (top_adj != top) tex_sketch_free(top_adj);
    if (btm_adj != btm) tex_sketch_free(btm_adj);
    tex_sketch_free(top);
    tex_sketch_free(btm);
    tex_sketch_free(ctr_new);
    tex_sketch_free(piled);

    return result;
}

//! Render fraction
static TexSketch *render_fraction(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    TexNode *node = &nodes->nodes[node_id];
    if (node->children.count < 2) return sketch_empty();

    TexSketch *numer = render_node(nodes, node->children.data[0], use_serif, inline_mode);
    TexSketch *denom = render_node(nodes, node->children.data[1], use_serif, inline_mode);

    // Inline mode: render as numer/denom on single line
    if (inline_mode) {
        TexRow *row = malloc(sizeof(TexRow));
        row_init(row);
        // Copy numerator cells
        for (int32_t i = 0; i < numer->rows[0].count; i++) {
            if (numer->rows[0].cells[i].data) {
                row_push(row, numer->rows[0].cells[i].data);
            }
        }
        row_push(row, "⁄");  // Fraction slash U+2044
        // Copy denominator cells
        for (int32_t i = 0; i < denom->rows[0].count; i++) {
            if (denom->rows[0].cells[i].data) {
                row_push(row, denom->rows[0].cells[i].data);
            }
        }
        tex_sketch_free(numer);
        tex_sketch_free(denom);
        return sketch_from_rows(row, 1, 0);
    }

    int32_t max_width = numer->rows[0].count;
    if (denom->rows[0].count > max_width) max_width = denom->rows[0].count;

    // Build fraction line
    TexRow *frac_row = malloc(sizeof(TexRow));
    row_init(frac_row);
    row_push(frac_row, "╶");
    for (int32_t i = 0; i < max_width; i++) {
        row_push(frac_row, "─");
    }
    row_push(frac_row, "╴");
    TexSketch *frac_line = sketch_from_rows(frac_row, 1, 0);

    TexSketch *result = util_vert_pile(numer, frac_line, 0, denom, TEX_ALIGN_CENTER);

    tex_sketch_free(numer);
    tex_sketch_free(denom);
    tex_sketch_free(frac_line);

    return result;
}

//! Render square root
static TexSketch *render_sqrt(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    TexNode *node = &nodes->nodes[node_id];
    if (node->children.count == 0) return sketch_empty();

    // Find radicand (last child) and optional degree (first child if > 1)
    int32_t rad_idx = node->children.count - 1;
    TexSketch *radicand = render_node(nodes, node->children.data[rad_idx], use_serif, inline_mode);
    TexSketch *degree = NULL;
    if (node->children.count > 1) {
        degree = render_node(nodes, node->children.data[0], use_serif, inline_mode);
    }

    // Inline mode or single-row radicand: use √x̅ form with combining overlines
    if (inline_mode || (radicand->height == 1 && radicand->rows[0].count <= 1)) {
        TexRow *row = malloc(sizeof(TexRow));
        row_init(row);
        row_push(row, "√");

        // Add all radicand characters with combining overline
        if (radicand->height == 1) {
            for (int32_t i = 0; i < radicand->rows[0].count; i++) {
                char buf[16];
                const char *rad_ch = radicand->rows[0].cells[i].data;
                snprintf(buf, sizeof(buf), "%s\u0305", rad_ch ? rad_ch : "");
                row_push(row, buf);
            }
        } else {
            // Multi-row radicand in inline mode: just use √(content)
            row_push(row, "(");
            for (int32_t r = 0; r < radicand->height; r++) {
                for (int32_t c = 0; c < radicand->rows[r].count; c++) {
                    if (radicand->rows[r].cells[c].data) {
                        row_push(row, radicand->rows[r].cells[c].data);
                    }
                }
            }
            row_push(row, ")");
        }

        TexSketch *result = sketch_from_rows(row, 1, 0);

        if (degree && !sketch_is_empty(degree)) {
            // Add degree as superscript
            TexSketch *deg_script = util_script(degree, 0);
            TexSketch *parts[] = {deg_script, result};
            TexSketch *final = util_concat(parts, 2, false, false);
            tex_sketch_free(deg_script);
            tex_sketch_free(result);
            result = final;
        }

        tex_sketch_free(radicand);
        if (degree) tex_sketch_free(degree);
        return result;
    }

    // Multi-line: use box drawing
    int32_t rad_width = radicand->rows[0].count;
    int32_t new_height = radicand->height + 1;

    TexRow *rows = calloc(new_height, sizeof(TexRow));

    // Top row: " ┌" + bars + "╴"
    row_init(&rows[0]);
    row_push(&rows[0], " ");
    row_push(&rows[0], "┌");
    for (int32_t i = 0; i < rad_width; i++) {
        row_push(&rows[0], "─");
    }
    row_push(&rows[0], "╴");

    // Content rows
    for (int32_t r = 0; r < radicand->height; r++) {
        row_init(&rows[r + 1]);

        if (r == radicand->height - 1) {
            // Bottom row with angle
            row_push(&rows[r + 1], "╰");
            row_push(&rows[r + 1], "┘");
        } else {
            // Side bar
            row_push(&rows[r + 1], " ");
            row_push(&rows[r + 1], "│");
        }

        // Content
        for (int32_t c = 0; c < radicand->rows[r].count; c++) {
            row_push(&rows[r + 1], radicand->rows[r].cells[c].data);
        }
        // Padding
        for (int32_t c = radicand->rows[r].count; c < rad_width; c++) {
            row_push(&rows[r + 1], TEX_BG);
        }
        row_push(&rows[r + 1], TEX_BG);
    }

    TexSketch *sqrt_sketch = sketch_from_rows(rows, new_height, radicand->horizon + 1);

    // Handle degree
    if (degree && !sketch_is_empty(degree) && degree->height == 1) {
        TexSketch *shrunk_deg = util_shrink(degree, 1, false, false);
        if (!shrunk_deg) shrunk_deg = sketch_clone(degree);

        int32_t deg_width = shrunk_deg->rows[0].count;

        // Insert degree in bottom-left area
        if (new_height >= 2 && deg_width > 0) {
            int32_t insert_row = new_height - 2;
            if (insert_row >= 0 && sqrt_sketch->rows[insert_row].count > 0) {
                // Pad left side with degree
                TexRow *new_rows = calloc(new_height, sizeof(TexRow));
                for (int32_t r = 0; r < new_height; r++) {
                    row_init(&new_rows[r]);
                    if (r == insert_row) {
                        // Add degree chars
                        for (int32_t c = 0; c < shrunk_deg->rows[0].count; c++) {
                            row_push(&new_rows[r], shrunk_deg->rows[0].cells[c].data);
                        }
                    } else {
                        // Add padding
                        for (int32_t c = 0; c < deg_width; c++) {
                            row_push(&new_rows[r], TEX_BG);
                        }
                    }
                    // Add original row
                    for (int32_t c = 0; c < sqrt_sketch->rows[r].count; c++) {
                        row_push(&new_rows[r], sqrt_sketch->rows[r].cells[c].data);
                    }
                }

                // Free old rows and replace
                for (int32_t r = 0; r < sqrt_sketch->height; r++) {
                    row_free(&sqrt_sketch->rows[r]);
                }
                free(sqrt_sketch->rows);
                sqrt_sketch->rows = new_rows;
            }
        }

        tex_sketch_free(shrunk_deg);
    }

    tex_sketch_free(radicand);
    if (degree) tex_sketch_free(degree);

    return sqrt_sketch;
}

//! Render binomial coefficient
static TexSketch *render_binom(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    TexNode *node = &nodes->nodes[node_id];
    if (node->children.count < 2) return sketch_empty();

    TexSketch *n = render_node(nodes, node->children.data[0], use_serif, inline_mode);
    TexSketch *r = render_node(nodes, node->children.data[1], use_serif, inline_mode);

    // Inline mode: render as C(n,r)
    if (inline_mode) {
        TexRow *row = malloc(sizeof(TexRow));
        row_init(row);
        row_push(row, "C");
        row_push(row, "(");
        for (int32_t c = 0; c < n->rows[0].count; c++) {
            if (n->rows[0].cells[c].data) row_push(row, n->rows[0].cells[c].data);
        }
        row_push(row, ",");
        for (int32_t c = 0; c < r->rows[0].count; c++) {
            if (r->rows[0].cells[c].data) row_push(row, r->rows[0].cells[c].data);
        }
        row_push(row, ")");
        tex_sketch_free(n);
        tex_sketch_free(r);
        return sketch_from_rows(row, 1, 0);
    }

    int32_t max_width = n->rows[0].count;
    if (r->rows[0].count > max_width) max_width = r->rows[0].count;

    // Create separator
    TexRow *sep_row = malloc(sizeof(TexRow));
    row_init(sep_row);
    for (int32_t i = 0; i < max_width; i++) {
        row_push(sep_row, TEX_BG);
    }
    TexSketch *sep = sketch_from_rows(sep_row, 1, 0);

    // Stack n over r
    TexSketch *piled = util_vert_pile(n, sep, 0, r, TEX_ALIGN_CENTER);

    // Add parentheses
    int32_t height = piled->height;
    int32_t horizon = piled->horizon;

    TexSketch *left = util_delimiter("(", height, horizon);
    TexSketch *right = util_delimiter(")", height, horizon);

    TexSketch *parts[] = {left, piled, right};
    TexSketch *result = util_concat(parts, 3, false, false);

    tex_sketch_free(n);
    tex_sketch_free(r);
    tex_sketch_free(sep);
    tex_sketch_free(piled);
    tex_sketch_free(left);
    tex_sketch_free(right);

    return result;
}

//! Render open delimiter (\left ... \right)
static TexSketch *render_open_delim(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    TexNode *node = &nodes->nodes[node_id];
    if (node->children.count < 2) return sketch_empty();

    // First and last children are delimiters
    int32_t left_id = node->children.data[0];
    int32_t right_id = node->children.data[node->children.count - 1];

    // Get delimiter characters
    TexSketch *left_leaf = render_node(nodes, left_id, use_serif, inline_mode);
    TexSketch *right_leaf = render_node(nodes, right_id, use_serif, inline_mode);
    const char *left_char = (left_leaf && left_leaf->height > 0 && left_leaf->rows[0].count > 0) ?
        left_leaf->rows[0].cells[0].data : "(";
    const char *right_char = (right_leaf && right_leaf->height > 0 && right_leaf->rows[0].count > 0) ?
        right_leaf->rows[0].cells[0].data : ")";

    // Render inside content
    int32_t inside_count = node->children.count - 2;
    TexSketch *inside;
    if (inside_count > 0) {
        TexSketch **parts = calloc(inside_count, sizeof(TexSketch *));
        for (int32_t i = 0; i < inside_count; i++) {
            parts[i] = render_node(nodes, node->children.data[i + 1], use_serif, inline_mode);
        }
        inside = util_concat(parts, inside_count, false, false);
        for (int32_t i = 0; i < inside_count; i++) {
            tex_sketch_free(parts[i]);
        }
        free(parts);
    } else {
        inside = sketch_empty();
    }

    // Inline mode: use single-char delimiters
    if (inline_mode) {
        TexRow *row = malloc(sizeof(TexRow));
        row_init(row);
        row_push(row, left_char);
        if (inside && inside->height > 0) {
            for (int32_t c = 0; c < inside->rows[0].count; c++) {
                if (inside->rows[0].cells[c].data) row_push(row, inside->rows[0].cells[c].data);
            }
        }
        row_push(row, right_char);
        tex_sketch_free(left_leaf);
        tex_sketch_free(right_leaf);
        tex_sketch_free(inside);
        return sketch_from_rows(row, 1, 0);
    }

    int32_t height = inside->height;
    int32_t horizon = inside->horizon;

    TexSketch *left = util_delimiter(left_char, height, horizon);
    TexSketch *right = util_delimiter(right_char, height, horizon);

    TexSketch *parts[] = {left, inside, right};
    TexSketch *result = util_concat(parts, 3, false, false);

    tex_sketch_free(left_leaf);
    tex_sketch_free(right_leaf);
    tex_sketch_free(inside);
    tex_sketch_free(left);
    tex_sketch_free(right);

    return result;
}

//! Render big delimiter
static TexSketch *render_big_delim(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    TexNode *node = &nodes->nodes[node_id];
    if (node->children.count < 1) return sketch_empty();

    TexSketch *delim_leaf = render_node(nodes, node->children.data[0], use_serif, inline_mode);
    const char *delim_type = (delim_leaf && delim_leaf->height > 0 && delim_leaf->rows[0].count > 0) ?
        delim_leaf->rows[0].cells[0].data : "(";

    // Inline mode: just use single char
    if (inline_mode) {
        TexRow *row = malloc(sizeof(TexRow));
        row_init(row);
        row_push(row, delim_type);
        tex_sketch_free(delim_leaf);
        return sketch_from_rows(row, 1, 0);
    }

    // Get height from command
    int32_t height = 1;
    const char *cmd = node->token.value;
    if (strcmp(cmd, "Big") == 0 || strcmp(cmd, "Bigl") == 0 || strcmp(cmd, "Bigr") == 0) height = 3;
    else if (strcmp(cmd, "bigg") == 0 || strcmp(cmd, "biggl") == 0 || strcmp(cmd, "biggr") == 0) height = 5;
    else if (strcmp(cmd, "Bigg") == 0 || strcmp(cmd, "Biggl") == 0 || strcmp(cmd, "Biggr") == 0) height = 7;

    TexSketch *result = util_delimiter(delim_type, height, height / 2);
    tex_sketch_free(delim_leaf);
    return result;
}

//! Render accent
static TexSketch *render_accent(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    TexNode *node = &nodes->nodes[node_id];
    if (node->children.count < 1) return sketch_empty();

    TexSketch *child = render_node(nodes, node->children.data[0], use_serif, inline_mode);
    if (!child || child->height == 0 || child->rows[0].count == 0) {
        return child ? child : sketch_empty();
    }

    const char *combining = tex_get_accent(node->token.value);
    if (!combining) return child;

    // Apply combining character to first character
    char *first_ch = child->rows[0].cells[0].data;
    if (first_ch) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s%s", first_ch, combining);
        free(child->rows[0].cells[0].data);
        child->rows[0].cells[0].data = tex_strdup(buf);
    }

    return child;
}

//! Render font command
static TexSketch *render_font_cmd(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    TexNode *node = &nodes->nodes[node_id];
    if (node->children.count < 1) return sketch_empty();

    TexSketch *child = render_node(nodes, node->children.data[0], use_serif, inline_mode);
    TexSketch *result = util_font(node->token.value, child);
    tex_sketch_free(child);
    return result;
}

//! Render line with ampersand alignment
static TexSketch *render_line_align_amp(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    TexNode *node = &nodes->nodes[node_id];
    int32_t child_count = node->children.count;

    if (child_count == 0) return sketch_empty();

    TexSketch **children = calloc(child_count, sizeof(TexSketch *));
    for (int32_t i = 0; i < child_count; i++) {
        children[i] = render_node(nodes, node->children.data[i], use_serif, inline_mode);
    }

    TexSketch *result = util_concat(children, child_count, true, true);

    for (int32_t i = 0; i < child_count; i++) {
        tex_sketch_free(children[i]);
    }
    free(children);

    return result;
}

//! Render line without ampersand alignment
static TexSketch *render_line_no_align(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    TexNode *node = &nodes->nodes[node_id];
    int32_t child_count = node->children.count;

    if (child_count == 0) return sketch_empty();

    TexSketch **children = calloc(child_count, sizeof(TexSketch *));
    for (int32_t i = 0; i < child_count; i++) {
        children[i] = render_node(nodes, node->children.data[i], use_serif, inline_mode);
    }

    TexSketch *result = util_concat(children, child_count, true, false);
    result->horizon = -2;  // Special marker for no ampersand alignment

    for (int32_t i = 0; i < child_count; i++) {
        tex_sketch_free(children[i]);
    }
    free(children);

    return result;
}

//! Render substack
static TexSketch *render_substack(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    TexNode *node = &nodes->nodes[node_id];
    if (node->children.count == 0) return sketch_empty();

    // Inline mode: render as comma-separated list
    if (inline_mode) {
        TexRow *row = malloc(sizeof(TexRow));
        row_init(row);
        for (int32_t i = 0; i < node->children.count; i++) {
            if (i > 0) row_push(row, ",");
            TexSketch *child = render_node(nodes, node->children.data[i], use_serif, inline_mode);
            if (child && child->height > 0) {
                for (int32_t c = 0; c < child->rows[0].count; c++) {
                    if (child->rows[0].cells[c].data) row_push(row, child->rows[0].cells[c].data);
                }
            }
            tex_sketch_free(child);
        }
        return sketch_from_rows(row, 1, 0);
    }

    // Render children and stack vertically
    TexSketch *result = render_node(nodes, node->children.data[0], use_serif, inline_mode);

    for (int32_t i = 1; i < node->children.count; i++) {
        TexSketch *child = render_node(nodes, node->children.data[i], use_serif, inline_mode);
        TexSketch *sep = sketch_empty();
        TexSketch *new_result = util_vert_pile(result, sep, 0, child, TEX_ALIGN_CENTER);
        tex_sketch_free(result);
        tex_sketch_free(child);
        tex_sketch_free(sep);
        result = new_result;
    }

    return result;
}

//! Render root (vertical stack of lines)
static TexSketch *render_root(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    TexNode *node = &nodes->nodes[node_id];
    if (node->children.count == 0) return sketch_empty();

    // Render first child
    TexSketch *result = render_node(nodes, node->children.data[0], use_serif, inline_mode);

    for (int32_t i = 1; i < node->children.count; i++) {
        TexSketch *child = render_node(nodes, node->children.data[i], use_serif, inline_mode);

        // Create separator (single background cell)
        TexRow *sep_row = malloc(sizeof(TexRow));
        row_init(sep_row);
        row_push(sep_row, TEX_BG);
        TexSketch *sep = sketch_from_rows(sep_row, 1, 0);

        TexSketch *new_result = util_vert_pile(result, sep, 0, child, TEX_ALIGN_LEFT);
        tex_sketch_free(result);
        tex_sketch_free(child);
        tex_sketch_free(sep);
        result = new_result;
    }

    return result;
}

//! Main node rendering dispatch
static TexSketch *render_node(TexNodeArray *nodes, int32_t node_id, bool use_serif, bool inline_mode) {
    if (node_id < 0 || node_id >= nodes->count) return sketch_empty();

    TexNode *node = &nodes->nodes[node_id];
    TexSketch *base = NULL;

    switch (node->type) {
        case TEX_NT_OPN_ROOT:
            base = render_root(nodes, node_id, use_serif, inline_mode);
            break;

        case TEX_NT_OPN_BRAC:
        case TEX_NT_OPN_ENVN:
        case TEX_NT_OPN_TEXT:
        case TEX_NT_OPN_DEGR:
            base = render_concat(nodes, node_id, use_serif, inline_mode);
            break;

        case TEX_NT_OPN_LINE:
        case TEX_NT_OPN_BRAK:
        case TEX_NT_OPN_PREN:
        case TEX_NT_OPN_DLLR:
        case TEX_NT_OPN_DDLR:
        case TEX_NT_OPN_STKLN:
        case TEX_NT_STK_LBRK:
            base = render_line_no_align(nodes, node_id, use_serif, inline_mode);
            break;

        case TEX_NT_CMD_LBRK:
            base = render_line_align_amp(nodes, node_id, use_serif, inline_mode);
            break;

        case TEX_NT_CMD_BGIN:
            // Check if align environment
            if (node->children.count > 0) {
                int32_t env_id = node->children.data[0];
                TexNode *env_node = &nodes->nodes[env_id];
                // Check for align environment name
                bool is_align = false;
                if (env_node->children.count >= 5) {
                    // Check if starts with "align"
                    is_align = true;  // Simplified check
                }
                if (is_align) {
                    // Render children[1:] with alignment
                    int32_t child_count = node->children.count - 1;
                    if (child_count > 0) {
                        TexSketch **children = calloc(child_count, sizeof(TexSketch *));
                        for (int32_t i = 0; i < child_count; i++) {
                            children[i] = render_node(nodes, node->children.data[i + 1], use_serif, inline_mode);
                        }
                        base = util_concat(children, child_count, true, true);
                        for (int32_t i = 0; i < child_count; i++) {
                            tex_sketch_free(children[i]);
                        }
                        free(children);
                    } else {
                        base = sketch_empty();
                    }
                } else {
                    base = render_line_no_align(nodes, node_id, use_serif, inline_mode);
                }
            } else {
                base = sketch_empty();
            }
            break;

        case TEX_NT_CMD_END:
            if (node->children.count > 0) {
                base = render_node(nodes, node->children.data[0], use_serif, inline_mode);
            } else {
                base = sketch_empty();
            }
            break;

        case TEX_NT_CMD_SQRT:
            base = render_sqrt(nodes, node_id, use_serif, inline_mode);
            break;

        case TEX_NT_CMD_FRAC:
            base = render_fraction(nodes, node_id, use_serif, inline_mode);
            break;

        case TEX_NT_CMD_BINOM:
            base = render_binom(nodes, node_id, use_serif, inline_mode);
            break;

        case TEX_NT_OPN_DLIM:
            base = render_open_delim(nodes, node_id, use_serif, inline_mode);
            break;

        case TEX_NT_CLS_DLIM:
            // Just render children
            if (node->children.count > 0) {
                base = render_node(nodes, node->children.data[0], use_serif, inline_mode);
            } else {
                base = sketch_empty();
            }
            break;

        case TEX_NT_BIG_DLIM:
            base = render_big_delim(nodes, node_id, use_serif, inline_mode);
            break;

        case TEX_NT_CMD_ACNT:
            base = render_accent(nodes, node_id, use_serif, inline_mode);
            break;

        case TEX_NT_CMD_FONT:
        case TEX_NT_CMD_TEXT:
            base = render_font_cmd(nodes, node_id, use_serif, inline_mode);
            break;

        case TEX_NT_CMD_SBSTK:
            base = render_substack(nodes, node_id, use_serif, inline_mode);
            break;

        case TEX_NT_SUP_SCRPT:
            if (node->children.count > 0) {
                TexSketch *child = render_node(nodes, node->children.data[0], use_serif, inline_mode);
                base = util_script(child, 0);
                tex_sketch_free(child);
            } else {
                base = sketch_empty();
            }
            break;

        case TEX_NT_SUB_SCRPT:
            if (node->children.count > 0) {
                TexSketch *child = render_node(nodes, node->children.data[0], use_serif, inline_mode);
                base = util_script(child, 1);
                tex_sketch_free(child);
            } else {
                base = sketch_empty();
            }
            break;

        case TEX_NT_TOP_SCRPT:
            // Try to shrink as subscript (for display above)
            if (node->children.count > 0) {
                TexSketch *child = render_node(nodes, node->children.data[0], use_serif, inline_mode);
                TexSketch *shrunk = util_shrink(child, 1, true, false);
                base = shrunk ? shrunk : sketch_clone(child);
                tex_sketch_free(child);
            } else {
                base = sketch_empty();
            }
            break;

        case TEX_NT_BTM_SCRPT:
            // Try to shrink as superscript (for display below)
            if (node->children.count > 0) {
                TexSketch *child = render_node(nodes, node->children.data[0], use_serif, inline_mode);
                TexSketch *shrunk = util_shrink(child, 0, true, false);
                base = shrunk ? shrunk : sketch_clone(child);
                tex_sketch_free(child);
            } else {
                base = sketch_empty();
            }
            break;

        case TEX_NT_CTR_BASE:
            base = render_ctr_base(&node->token);
            break;

        case TEX_NT_TXT_LEAF:
        case TEX_NT_TXT_INFO:
        case TEX_NT_CMD_LEAF:
            base = render_leaf(&node->token, use_serif);
            break;

        default:
            base = sketch_empty();
            break;
    }

    // Apply scripts
    if (node->scripts.count > 0) {
        TexSketch *with_scripts = render_apply_scripts(base, nodes, &node->scripts, use_serif, inline_mode);
        tex_sketch_free(base);
        return with_scripts;
    }

    return base;
}

static TexSketch *tex_render_internal(TexNodeArray *nodes, bool use_serif_italic, bool inline_mode) {
    if (!nodes || nodes->count == 0) return sketch_empty();
    return render_node(nodes, 0, use_serif_italic, inline_mode);
}

TexSketch *tex_render(TexNodeArray *nodes, bool use_serif_italic) {
    return tex_render_internal(nodes, use_serif_italic, false);
}

// #endregion

// #region Public API

static TexSketch *tex_render_string_internal(const char *latex, size_t len, bool use_serif_italic, bool inline_mode) {
    if (!latex || len == 0) return sketch_empty();

    // Tokenize
    int32_t token_count = 0;
    TexToken *tokens = tex_lex(latex, len, &token_count);
    if (!tokens || token_count == 0) {
        free(tokens);
        return sketch_empty();
    }

    // Parse
    TexNodeArray *nodes = tex_parse(tokens, token_count);
    free(tokens);

    if (!nodes || nodes->count == 0) {
        tex_nodes_free(nodes);
        return sketch_empty();
    }

    // Render
    TexSketch *result = tex_render_internal(nodes, use_serif_italic, inline_mode);
    tex_nodes_free(nodes);

    return result;
}

TexSketch *tex_render_string(const char *latex, size_t len, bool use_serif_italic) {
    return tex_render_string_internal(latex, len, use_serif_italic, false);
}

TexSketch *tex_render_inline(const char *latex, size_t len, bool use_serif_italic) {
    return tex_render_string_internal(latex, len, use_serif_italic, true);
}

// #endregion
