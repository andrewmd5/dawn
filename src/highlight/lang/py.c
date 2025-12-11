//! @file lang/py.c
//! @brief Python language definition

#include "../highlight.h"

static const hl_lang_rule_t rules[] = {
    { .pattern = "#.*(?:\\n|$)", .token = HL_TOKEN_CMNT },
    // Docstrings
    { .pattern = "(\"\"\"|''')(?:\\\\[\\s\\S]|(?!\\1)[\\s\\S])*\\1?", .token = HL_TOKEN_CMNT },
    // f-strings, r-strings, b-strings, fr-strings etc.
    { .pattern = "[fFrRbBuU]{1,2}([\"'])(?:\\\\[\\s\\S]|(?!\\1).)*\\1?", .token = HL_TOKEN_STR },
    { .pattern = "([\"'])(?:\\\\[\\s\\S]|(?!\\1)[^\\r\\n\\\\])*\\1?", .token = HL_TOKEN_STR },
    // Keywords
    { .pattern = "\\b(?:and|as|assert|async|await|break|class|continue|def|del|elif|else|except|finally|for|from|global|if|import|in|is|lambda|nonlocal|not|or|pass|raise|return|try|while|with|yield|match|case)\\b", .token = HL_TOKEN_KWD },
    // Built-in types
    { .pattern = "\\b(?:int|float|complex|str|bytes|bytearray|bool|list|tuple|set|frozenset|dict|range|slice|object|type|memoryview)\\b", .token = HL_TOKEN_TYPE },
    // Common typing types
    { .pattern = "\\b(?:Any|Union|Optional|List|Dict|Set|Tuple|Callable|Iterator|Generator|Coroutine|Type|Sequence|Mapping|MutableMapping|Iterable|Awaitable)\\b", .token = HL_TOKEN_TYPE },
    // Exception types
    { .pattern = "\\b(?:Exception|BaseException|ValueError|TypeError|KeyError|IndexError|AttributeError|RuntimeError|StopIteration|GeneratorExit|AssertionError|ImportError|ModuleNotFoundError|OSError|IOError|FileNotFoundError|PermissionError|NotImplementedError|ZeroDivisionError)\\b", .token = HL_TOKEN_TYPE },
    // Boolean and None
    { .pattern = "\\b(?:True|False)\\b", .token = HL_TOKEN_BOOL },
    { .pattern = "\\bNone\\b", .token = HL_TOKEN_NUM },
    // Numbers
    { .pattern = "(?:\\.e?|\\b)\\d(?:e-|[\\d.oxa-fA-F_])*[jJ]?(?:\\.|\\b)", .token = HL_TOKEN_NUM },
    // Built-in functions
    { .pattern = "\\b(?:abs|all|any|ascii|bin|bool|breakpoint|bytearray|bytes|callable|chr|classmethod|compile|complex|delattr|dict|dir|divmod|enumerate|eval|exec|filter|float|format|frozenset|getattr|globals|hasattr|hash|help|hex|id|input|int|isinstance|issubclass|iter|len|list|locals|map|max|memoryview|min|next|object|oct|open|ord|pow|print|property|range|repr|reversed|round|set|setattr|slice|sorted|staticmethod|str|sum|super|tuple|type|vars|zip)\\b(?=\\s*\\()", .token = HL_TOKEN_FUNC },
    // Decorators
    { .pattern = "@[a-zA-Z_][\\w.]*", .token = HL_TOKEN_FUNC },
    // Function/method calls
    { .pattern = "[a-z_]\\w*(?=\\s*\\()", .token = HL_TOKEN_FUNC, .flags = HL_RULE_CASELESS },
    { .pattern = "[-/*+<>,=!&|^%:]+|\\*\\*|//", .token = HL_TOKEN_OPER },
    { .pattern = "\\b[A-Z][\\w_]*\\b", .token = HL_TOKEN_CLASS },
    // Self and cls
    { .pattern = "\\b(?:self|cls)\\b", .token = HL_TOKEN_VAR },
};

static const hl_detect_rule_t detect[] = {
    { "#!(/usr)?/bin/(python|python3)", 500 },
    { "\\b(def|print|class|and|or|lambda)\\b", 10 },
};

static const hl_lang_def_t lang = {
    .name = "py",
    .rules = rules,
    .rule_count = sizeof(rules) / sizeof(rules[0]),
    .default_token = HL_TOKEN_NONE,
    .detect = detect,
    .detect_count = sizeof(detect) / sizeof(detect[0]),
};

const hl_lang_def_t *hl_lang_py(void) { return &lang; }
