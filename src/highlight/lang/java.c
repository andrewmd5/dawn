//! @file lang/java.c
//! @brief Java language definition

#include "../highlight.h"

static const hl_lang_rule_t rules[] = {
    { .pattern = "//.*(?:\\n|$)|/\\*(?:(?!\\*/).|[\\s\\S])*(?:\\*/)?", .token = HL_TOKEN_CMNT },
    { .pattern = "([\"'])(?:\\\\[\\s\\S]|(?!\\1)[^\\r\\n\\\\])*\\1?", .token = HL_TOKEN_STR },
    { .pattern = "(?:\\.e?|\\b)\\d(?:e-|[\\d.oxa-fA-F_])*[dDfFlL]?(?:\\.|\\b)", .token = HL_TOKEN_NUM },
    // Annotations
    { .pattern = "@[A-Za-z_][\\w]*(?:\\([^)]*\\))?", .token = HL_TOKEN_FUNC },
    // Keywords (non-type)
    { .pattern = "\\b(?:abstract|assert|break|case|catch|class|continue|const|default|do|else|enum|exports|extends|final|finally|for|goto|if|implements|import|instanceof|interface|module|native|new|package|private|protected|public|requires|return|static|strictfp|super|switch|synchronized|this|throw|throws|transient|try|var|volatile|while)\\b", .token = HL_TOKEN_KWD },
    // Primitive types
    { .pattern = "\\b(?:boolean|byte|char|double|float|int|long|short|void)\\b", .token = HL_TOKEN_TYPE },
    // Common Java types
    { .pattern = "\\b(?:String|Integer|Long|Double|Float|Boolean|Byte|Short|Character|Object|Class|Number|Void)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:List|ArrayList|LinkedList|Map|HashMap|TreeMap|Set|HashSet|TreeSet|Collection|Iterator|Iterable|Queue|Deque|Stack|Vector)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:Optional|Stream|Supplier|Consumer|Function|Predicate|BiFunction|BiConsumer|BiPredicate)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:Exception|RuntimeException|Error|Throwable|IOException|NullPointerException|IllegalArgumentException|IllegalStateException)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:Thread|Runnable|Callable|Future|CompletableFuture|ExecutorService|Executor)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "[/*+:?&|%^~=!,<>.^-]+", .token = HL_TOKEN_OPER },
    { .pattern = "[a-zA-Z_][\\w_]*(?=\\s*\\()", .token = HL_TOKEN_FUNC },
    { .pattern = "\\b[A-Z][\\w_]*\\b", .token = HL_TOKEN_CLASS },
    { .pattern = "\\b(?:true|false)\\b", .token = HL_TOKEN_BOOL },
    { .pattern = "\\bnull\\b", .token = HL_TOKEN_NUM },
};

static const hl_detect_rule_t detect[] = {
    { "^import\\s+java", 500 },
};

static const hl_lang_def_t lang = {
    .name = "java",
    .rules = rules,
    .rule_count = sizeof(rules) / sizeof(rules[0]),
    .default_token = HL_TOKEN_NONE,
    .detect = detect,
    .detect_count = sizeof(detect) / sizeof(detect[0]),
};

const hl_lang_def_t *hl_lang_java(void) { return &lang; }
