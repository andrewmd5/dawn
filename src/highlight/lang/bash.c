//! @file lang/bash.c
//! @brief Bash/Shell language definition

#include "../highlight.h"

static const hl_lang_rule_t rules[] = {
    { .pattern = "#.*(?:\\n|$)", .token = HL_TOKEN_CMNT },
    { .pattern = "([\"'])(?:\\\\[\\s\\S]|(?!\\1)[^\\r\\n\\\\])*\\1?", .token = HL_TOKEN_STR },
    // Flags
    { .pattern = "\\s-{1,2}[a-zA-Z][a-zA-Z0-9_-]*", .token = HL_TOKEN_VAR },
    // Shell keywords
    { .pattern = "\\b(?:if|fi|else|elif|while|do|done|for|until|case|esac|break|continue|exit|return|trap|wait|eval|exec|then|in|function|select|coproc)\\b", .token = HL_TOKEN_KWD },
    // Shell builtins
    { .pattern = "\\b(?:unset|readonly|shift|export|declare|enable|local|typeset|time|source|alias|unalias|set|shopt|cd|pwd|pushd|popd|dirs|jobs|fg|bg|kill|disown|suspend|logout|history|fc|bind|builtin|caller|command|compgen|complete|compopt|getopts|hash|help|let|mapfile|printf|read|readarray|test|times|type|ulimit|umask|echo)\\b", .token = HL_TOKEN_FUNC },
    // Common commands
    { .pattern = "\\b(?:ls|cat|grep|sed|awk|find|xargs|sort|uniq|wc|head|tail|cut|tr|tee|diff|patch|tar|gzip|gunzip|zip|unzip|curl|wget|ssh|scp|rsync|chmod|chown|chgrp|mkdir|rmdir|rm|cp|mv|ln|touch|stat|file|which|whereis|locate|man|less|more|nano|vim|vi|emacs|git|make|cmake|gcc|g\\+\\+|clang|python|python3|pip|npm|node|yarn|ruby|gem|cargo|rustc|go|java|javac|docker|kubectl)\\b", .token = HL_TOKEN_FUNC },
    // Numbers
    { .pattern = "(?:\\.e?|\\b)\\d(?:e-|[\\d.oxa-fA-F_])*(?:\\.|\\b)", .token = HL_TOKEN_NUM },
    { .pattern = "\\b(?:true|false)\\b", .token = HL_TOKEN_BOOL },
    // Operators
    { .pattern = "[=(){}<>!]+|[&|;]+|\\[\\[|\\]\\]", .token = HL_TOKEN_OPER },
    // Variables
    { .pattern = "\\$\\w+|\\$\\{[^}]*\\}|\\$\\([^)]*\\)", .token = HL_TOKEN_VAR },
    // Environment variables (all-caps)
    { .pattern = "\\b[A-Z_][A-Z0-9_]+\\b", .token = HL_TOKEN_VAR },
};

static const hl_detect_rule_t detect[] = {
    { "#!(/usr)?/bin/bash", 500 },
    { "\\b(if|elif|then|fi|echo)\\b|\\$", 10 },
};

static const hl_lang_def_t lang = {
    .name = "bash",
    .rules = rules,
    .rule_count = sizeof(rules) / sizeof(rules[0]),
    .default_token = HL_TOKEN_NONE,
    .detect = detect,
    .detect_count = sizeof(detect) / sizeof(detect[0]),
};

const hl_lang_def_t *hl_lang_bash(void) { return &lang; }
