//! @file lang/js.c
//! @brief JavaScript language definition

#include "../highlight.h"

static const hl_lang_rule_t rules[] = {
    // JSDoc comments
    { .pattern = "/\\*\\*(?:(?!\\*/).|[\\s\\S])*(?:\\*/)?", .token = HL_TOKEN_CMNT },
    { .pattern = "//.*(?:\\n|$)|/\\*(?:(?!\\*/).|[\\s\\S])*(?:\\*/)?", .token = HL_TOKEN_CMNT },
    { .pattern = "`(?:[^`\\\\]|\\\\[\\s\\S])*`?", .token = HL_TOKEN_STR },
    { .pattern = "([\"'])(?:\\\\[\\s\\S]|(?!\\1)[^\\r\\n\\\\])*\\1?", .token = HL_TOKEN_STR },
    // Keywords
    { .pattern = "=>|\\b(?:this|set|get|as|async|await|break|case|catch|class|const|constructor|continue|debugger|default|delete|do|else|enum|export|extends|finally|for|from|function|if|implements|import|in|instanceof|interface|let|var|of|new|package|private|protected|public|return|static|super|switch|throw|throws|try|typeof|void|while|with|yield)\\b", .token = HL_TOKEN_KWD },
    // Regex literals
    { .pattern = "/(?!/)[^\\r\\n\\\\]+(?:\\\\.[^\\r\\n\\\\]*)*/[dgimsuy]*", .token = HL_TOKEN_STR },
    // Built-in objects/types
    { .pattern = "\\b(?:Array|Object|String|Number|Boolean|Function|Symbol|BigInt|Date|RegExp|Error|Map|Set|WeakMap|WeakSet|Promise|Proxy|Reflect|JSON|Math|Intl|ArrayBuffer|SharedArrayBuffer|DataView|Float32Array|Float64Array|Int8Array|Int16Array|Int32Array|Uint8Array|Uint16Array|Uint32Array|Uint8ClampedArray|BigInt64Array|BigUint64Array)\\b", .token = HL_TOKEN_TYPE },
    // Global objects
    { .pattern = "\\b(?:console|window|document|navigator|location|history|localStorage|sessionStorage|fetch|XMLHttpRequest|WebSocket|Worker|Blob|File|FileReader|URL|URLSearchParams|FormData|AbortController|Request|Response|Headers)\\b", .token = HL_TOKEN_TYPE },
    // Numbers
    { .pattern = "(?:\\.e?|\\b)\\d(?:e-|[\\d.oxa-fA-F_])*n?(?:\\.|\\b)", .token = HL_TOKEN_NUM },
    { .pattern = "\\b(?:NaN|Infinity)\\b", .token = HL_TOKEN_NUM },
    { .pattern = "\\b(?:null|undefined)\\b", .token = HL_TOKEN_NUM },
    // All-caps constants
    { .pattern = "\\b[A-Z][A-Z_0-9]+\\b", .token = HL_TOKEN_NUM },
    { .pattern = "\\b(?:true|false)\\b", .token = HL_TOKEN_BOOL },
    { .pattern = "[/*+:?&|%^~=!,<>.^-]+", .token = HL_TOKEN_OPER },
    { .pattern = "\\b[A-Z][\\w_]*\\b", .token = HL_TOKEN_CLASS },
    { .pattern = "[a-zA-Z$_][\\w$_]*(?=\\s*(?:(?:\\?\\.)?\\s*\\(|=\\s*(?:\\(?[\\w,{}\\[\\])]+\\)?\\s*=>|function\\b)))", .token = HL_TOKEN_FUNC },
};

static const hl_detect_rule_t detect[] = {
    { "\\b(console|await|async|function|export|import|this|class|for|let|const|map|join|require)\\b", 10 },
};

static const hl_lang_def_t lang = {
    .name = "js",
    .rules = rules,
    .rule_count = sizeof(rules) / sizeof(rules[0]),
    .default_token = HL_TOKEN_NONE,
    .detect = detect,
    .detect_count = sizeof(detect) / sizeof(detect[0]),
};

const hl_lang_def_t *hl_lang_js(void) { return &lang; }
