//! @file lang/go.c
//! @brief Go language definition

#include "../highlight.h"

static const hl_lang_rule_t rules[] = {
    { .pattern = "//.*(?:\\n|$)|/\\*(?:(?!\\*/).|[\\s\\S])*(?:\\*/)?", .token = HL_TOKEN_CMNT },
    { .pattern = "([\"'])(?:\\\\[\\s\\S]|(?!\\1)[^\\r\\n\\\\])*\\1?", .token = HL_TOKEN_STR },
    { .pattern = "`[^`]*`", .token = HL_TOKEN_STR },
    { .pattern = "(?:\\.e?|\\b)\\d(?:e-|[\\d.oxa-fA-F_])*(?:\\.|\\b)", .token = HL_TOKEN_NUM },
    // Keywords
    { .pattern = "\\b(?:break|case|chan|const|continue|default|defer|else|fallthrough|for|func|go|goto|if|import|interface|map|package|range|return|select|struct|switch|type|var)\\b", .token = HL_TOKEN_KWD },
    // Builtin types
    { .pattern = "\\b(?:bool|byte|complex64|complex128|error|float32|float64|int|int8|int16|int32|int64|rune|string|uint|uint8|uint16|uint32|uint64|uintptr|any|comparable)\\b", .token = HL_TOKEN_TYPE },
    // Common standard library types
    { .pattern = "\\b(?:Reader|Writer|ReadWriter|Closer|ReadCloser|WriteCloser|ReadWriteCloser|Seeker)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:Context|Duration|Time|Timer|Ticker)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:Mutex|RWMutex|WaitGroup|Once|Cond|Pool|Map)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:Buffer|Builder|Regexp|File)\\b", .token = HL_TOKEN_TYPE },
    // Builtin functions
    { .pattern = "\\b(?:append|cap|close|complex|copy|delete|imag|len|make|new|panic|print|println|real|recover)\\b(?=\\s*\\()", .token = HL_TOKEN_FUNC },
    // Boolean and nil
    { .pattern = "\\b(?:true|false)\\b", .token = HL_TOKEN_BOOL },
    { .pattern = "\\bnil\\b", .token = HL_TOKEN_NUM },
    // Iota
    { .pattern = "\\biota\\b", .token = HL_TOKEN_NUM },
    // Function calls
    { .pattern = "[a-zA-Z_][\\w_]*(?=\\s*\\()", .token = HL_TOKEN_FUNC },
    { .pattern = "\\b[A-Z][\\w_]*\\b", .token = HL_TOKEN_CLASS },
    { .pattern = "[+\\-*/%&|^~=!<>:]+|<-", .token = HL_TOKEN_OPER },
};

static const hl_detect_rule_t detect[] = {
    { "\\b(func|fmt|package)\\b", 100 },
};

static const hl_lang_def_t lang = {
    .name = "go",
    .rules = rules,
    .rule_count = sizeof(rules) / sizeof(rules[0]),
    .default_token = HL_TOKEN_NONE,
    .detect = detect,
    .detect_count = sizeof(detect) / sizeof(detect[0]),
};

const hl_lang_def_t *hl_lang_go(void) { return &lang; }
