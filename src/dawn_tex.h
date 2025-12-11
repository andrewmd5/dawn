// dawn_tex.h

#ifndef DAWN_TEX_H
#define DAWN_TEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dawn_backend.h"

// #region Constants

#define TEX_MAX_TOKEN_LEN 64
#define TEX_INITIAL_TOKEN_CAPACITY 128
#define TEX_INITIAL_NODE_CAPACITY 128
#define TEX_INITIAL_STACK_CAPACITY 64
#define TEX_INITIAL_ROW_CAPACITY 64

//! Alignment constants
DAWN_ENUM(uint8_t) {
    TEX_ALIGN_LEFT = 0,
    TEX_ALIGN_CENTER,
    TEX_ALIGN_RIGHT,
} TexAlign;

//! Delimiter position constants
DAWN_ENUM(uint8_t) {
    TEX_DELIM_SGL = 0,  //! Single-line
    TEX_DELIM_TOP,      //! Top of multi-line
    TEX_DELIM_CTR,      //! Center of multi-line
    TEX_DELIM_FIL,      //! Fill (mid sections)
    TEX_DELIM_BTM,      //! Bottom of multi-line
} TexDelimPos;

// #endregion

// #region Token Types

DAWN_ENUM(uint8_t) {
    TEX_TOK_NONE = 0,
    TEX_TOK_ALPH,    //! Alphabetic character
    TEX_TOK_NUMB,    //! Numeric character
    TEX_TOK_SYMB,    //! Symbol (^, _, {, }, etc.)
    TEX_TOK_CMND,    //! Command (\frac, \sqrt, etc.)
    TEX_TOK_META,    //! Meta tokens (start, end, startline, endline)
} TexTokenType;

typedef struct {
    TexTokenType type;
    char value[TEX_MAX_TOKEN_LEN];
    int32_t value_len;
} TexToken;

// #endregion

// #region Node Types

DAWN_ENUM(uint8_t) {
    TEX_NT_NONE = 0,

    // Container nodes
    TEX_NT_OPN_ROOT,    //! Root container
    TEX_NT_OPN_BRAC,    //! { } group
    TEX_NT_OPN_DEGR,    //! [ ] degree (for sqrt)
    TEX_NT_OPN_DLIM,    //! \left delimiter
    TEX_NT_OPN_LINE,    //! Line in expression
    TEX_NT_OPN_BRAK,    //! \[ \] display math
    TEX_NT_OPN_PREN,    //! \( \) inline math
    TEX_NT_OPN_DLLR,    //! $ inline math
    TEX_NT_OPN_DDLR,    //! $$ display math
    TEX_NT_OPN_ENVN,    //! Environment name
    TEX_NT_OPN_TEXT,    //! Text mode
    TEX_NT_OPN_STKLN,   //! Substack line

    // Command nodes
    TEX_NT_CMD_SQRT,    //! \sqrt
    TEX_NT_CMD_FRAC,    //! \frac
    TEX_NT_CMD_BINOM,   //! \binom
    TEX_NT_CMD_FONT,    //! \mathbf, etc.
    TEX_NT_CMD_ACNT,    //! \hat, \vec, etc.
    TEX_NT_CMD_TEXT,    //! \text
    TEX_NT_CMD_SBSTK,   //! \substack
    TEX_NT_CMD_BGIN,    //! \begin
    TEX_NT_CMD_END,     //! \end
    TEX_NT_CMD_LBRK,    //! \\ or \newline
    TEX_NT_CMD_LMTS,    //! \limits
    TEX_NT_CMD_STYL,    //! Style commands

    // Script nodes
    TEX_NT_SUP_SCRPT,   //! ^ superscript
    TEX_NT_SUB_SCRPT,   //! _ subscript
    TEX_NT_TOP_SCRPT,   //! Top script (for \sum, etc.)
    TEX_NT_BTM_SCRPT,   //! Bottom script (for \sum, etc.)

    // Delimiter nodes
    TEX_NT_BIG_DLIM,    //! \big, \Big, etc.
    TEX_NT_CLS_DLIM,    //! \right

    // Leaf nodes
    TEX_NT_TXT_LEAF,    //! Text/symbol leaf
    TEX_NT_TXT_INFO,    //! Environment info
    TEX_NT_TXT_INVS,    //! Invisible (space)
    TEX_NT_CMD_LEAF,    //! Command leaf
    TEX_NT_CTR_BASE,    //! Center base (\sum, \prod, \lim)

    // Close nodes
    TEX_NT_CLS_ROOT,
    TEX_NT_CLS_BRAC,
    TEX_NT_CLS_DEGR,
    TEX_NT_CLS_LINE,
    TEX_NT_CLS_BRAK,
    TEX_NT_CLS_PREN,
    TEX_NT_CLS_DLLR,
    TEX_NT_CLS_DDLR,
    TEX_NT_CLS_ENVN,
    TEX_NT_CLS_TEXT,
    TEX_NT_CLS_STKLN,

    // Special
    TEX_NT_STK_LBRK,    //! Substack line break
} TexNodeType;

// #endregion

// #region AST Structures

//! Dynamic array of child indices
typedef struct {
    int32_t *data;
    int32_t count;
    int32_t capacity;
} TexIdArray;

//! AST Node using flat structure with index references
typedef struct {
    TexNodeType type;
    TexToken token;
    TexIdArray children;   //! Child node indices
    TexIdArray scripts;    //! Script node indices
} TexNode;

//! Flat node array for the entire AST
typedef struct {
    TexNode *nodes;
    int32_t count;
    int32_t capacity;
} TexNodeArray;

// #endregion

// #region Rendered Sketch

//! Single character cell (UTF-8 string, typically 1-4 bytes)
typedef struct {
    char *data;      //! UTF-8 character (heap allocated)
} TexCell;

//! Row of cells
typedef struct {
    TexCell *cells;
    int32_t count;
    int32_t capacity;
} TexRow;

//! 2D Unicode art with baseline tracking
typedef struct {
    TexRow *rows;     //! Array of rows
    int32_t height;       //! Number of rows
    int32_t width;        //! Width (cells per row)
    int32_t horizon;      //! Baseline row index (for alignment)
} TexSketch;

// #endregion

// #region Font Options

DAWN_ENUM(uint8_t) {
    TEX_FONT_NORMAL = 0,
    TEX_FONT_SERIF_IT,
    TEX_FONT_SERIF_BLD,
    TEX_FONT_SERIF_ITBD,
    TEX_FONT_SANS,
    TEX_FONT_SANS_IT,
    TEX_FONT_SANS_BLD,
    TEX_FONT_SANS_ITBD,
    TEX_FONT_MONO,
    TEX_FONT_CALI,
    TEX_FONT_FRAK,
    TEX_FONT_DOUBLE,
} TexFontStyle;

// #endregion

// #region Public API

//! Render LaTeX string to Unicode art
//! @param latex LaTeX input string
//! @param len Length of input
//! @param use_serif_italic Use serif italic for variables (true) or normal (false)
//! @return Allocated TexSketch (caller must free with tex_sketch_free)
TexSketch *tex_render_string(const char *latex, size_t len, bool use_serif_italic);

//! Render LaTeX string for inline display (single-line output)
//! @param latex LaTeX input string
//! @param len Length of input
//! @param use_serif_italic Use serif italic for variables (true) or normal (false)
//! @return Allocated TexSketch guaranteed to be 1 row (caller must free with tex_sketch_free)
TexSketch *tex_render_inline(const char *latex, size_t len, bool use_serif_italic);

//! Free a rendered sketch
void tex_sketch_free(TexSketch *s);

//! Create an empty sketch with given dimensions
TexSketch *tex_sketch_new(int32_t height, int32_t width);

//! Print sketch to stdout (for debugging)
void tex_sketch_print(const TexSketch *s);

//! Convert sketch to single string (caller must free)
char *tex_sketch_to_string(const TexSketch *s);

// #endregion

// #region Symbol Table Access (from dawn_tex_symbols.c)

//! Get alphabet string for a font style
const char *tex_get_alphabet(TexFontStyle style);

//! Get font style for a command
TexFontStyle tex_get_font_style(const char *name);

//! Lookup command type from name
TexNodeType tex_lookup_cmd_type(const char *name);

//! Get parent-dependent type (returns TEX_NT_NONE if not found)
TexNodeType tex_get_parent_dep_type(TexNodeType parent, TexTokenType tok_type, const char *tok_value);

// #endregion

// #region Internal Functions (exposed for testing)

//! Tokenize LaTeX string
//! @param input LaTeX string
//! @param len Length of input
//! @param out_count Output: number of tokens
//! @return Array of tokens (caller must free)
TexToken *tex_lex(const char *input, size_t len, int32_t *out_count);

//! Parse tokens into flat node array
//! @param tokens Token array from tex_lex
//! @param count Number of tokens
//! @return Node array (caller must free with tex_nodes_free)
TexNodeArray *tex_parse(TexToken *tokens, int32_t count);

//! Render node array to sketch
//! @param nodes Node array from tex_parse
//! @param use_serif_italic Font option
//! @return Rendered sketch
TexSketch *tex_render(TexNodeArray *nodes, bool use_serif_italic);

//! Free node array and all nodes
void tex_nodes_free(TexNodeArray *nodes);

// #endregion

#endif // DAWN_TEX_H
