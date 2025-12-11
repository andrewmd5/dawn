//! @file lang/c.c
//! @brief C language definition

#include "../highlight.h"

static const hl_lang_rule_t rules[] = {
    { .pattern = "//.*(?:\\n|$)|/\\*(?:(?!\\*/).|[\\s\\S])*(?:\\*/)?", .token = HL_TOKEN_CMNT },
    { .pattern = "([\"'])(?:\\\\[\\s\\S]|(?!\\1)[^\\r\\n\\\\])*\\1?", .token = HL_TOKEN_STR },
    { .pattern = "'(?:\\\\[\\s\\S]|[^'\\\\])'", .token = HL_TOKEN_STR },
    { .pattern = "(?:\\.e?|\\b)\\d(?:e-|[\\d.oxa-fA-F_])*[uUlLfF]*(?:\\.|\\b)", .token = HL_TOKEN_NUM },
    { .pattern = "#\\s*include\\s*(?:<[^>]*>|\"[^\"]*\")", .token = HL_TOKEN_KWD },
    { .pattern = "#\\s*(?:define|undef|ifdef|ifndef|if|elif|else|endif|error|pragma|warning|line)\\b", .token = HL_TOKEN_KWD },
    { .pattern = "\\b(?:auto|break|case|const|continue|default|do|else|extern|for|goto|if|inline|register|restrict|return|sizeof|static|switch|typedef|volatile|while|_Alignas|_Alignof|_Atomic|_Generic|_Noreturn|_Static_assert|_Thread_local)\\b", .token = HL_TOKEN_KWD },
    // Standard C types and stdint types
    { .pattern = "\\b(?:void|char|short|int|long|float|double|signed|unsigned|enum|struct|union|_Bool|_Complex|_Imaginary)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:size_t|ssize_t|ptrdiff_t|intptr_t|uintptr_t|intmax_t|uintmax_t|wchar_t|char16_t|char32_t)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:int8_t|int16_t|int32_t|int64_t|uint8_t|uint16_t|uint32_t|uint64_t)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:int_least8_t|int_least16_t|int_least32_t|int_least64_t|uint_least8_t|uint_least16_t|uint_least32_t|uint_least64_t)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:int_fast8_t|int_fast16_t|int_fast32_t|int_fast64_t|uint_fast8_t|uint_fast16_t|uint_fast32_t|uint_fast64_t)\\b", .token = HL_TOKEN_TYPE },
    // POSIX and common types
    { .pattern = "\\b(?:pid_t|uid_t|gid_t|off_t|mode_t|dev_t|ino_t|nlink_t|blksize_t|blkcnt_t|time_t|clock_t|suseconds_t)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:socklen_t|sa_family_t|in_addr_t|in_port_t)\\b", .token = HL_TOKEN_TYPE },
    // Common C library types
    { .pattern = "\\b(?:FILE|DIR|va_list|jmp_buf|sig_atomic_t|fpos_t|div_t|ldiv_t|lldiv_t|mbstate_t)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:errno_t|rsize_t|max_align_t|nullptr_t)\\b", .token = HL_TOKEN_TYPE },
    // Atomic types
    { .pattern = "\\b(?:atomic_bool|atomic_char|atomic_schar|atomic_uchar|atomic_short|atomic_ushort|atomic_int|atomic_uint|atomic_long|atomic_ulong|atomic_llong|atomic_ullong)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:atomic_intptr_t|atomic_uintptr_t|atomic_size_t|atomic_ptrdiff_t|atomic_intmax_t|atomic_uintmax_t)\\b", .token = HL_TOKEN_TYPE },
    // pthread types
    { .pattern = "\\b(?:pthread_t|pthread_attr_t|pthread_mutex_t|pthread_mutexattr_t|pthread_cond_t|pthread_condattr_t|pthread_key_t|pthread_once_t|pthread_rwlock_t|pthread_rwlockattr_t|pthread_spinlock_t|pthread_barrier_t|pthread_barrierattr_t)\\b", .token = HL_TOKEN_TYPE },
    // Boolean and null
    { .pattern = "\\b(?:true|false)\\b", .token = HL_TOKEN_BOOL },
    { .pattern = "\\bNULL\\b", .token = HL_TOKEN_NUM },
    // Operators
    { .pattern = "[*&]", .token = HL_TOKEN_OPER },
    { .pattern = "[/*+:?|%^~=!,<>.^-]+", .token = HL_TOKEN_OPER },
    // Function calls
    { .pattern = "[a-zA-Z_][\\w_]*(?=\\s*\\()", .token = HL_TOKEN_FUNC },
    // Uppercase identifiers (macros, constants, types)
    { .pattern = "\\b[A-Z][A-Z0-9_]*\\b", .token = HL_TOKEN_NUM },
    // PascalCase identifiers (types)
    { .pattern = "\\b[A-Z][a-z][\\w_]*\\b", .token = HL_TOKEN_CLASS },
};

static const hl_detect_rule_t detect[] = {
    { "#include\\b|\\bprintf\\s*\\(", 100 },
};

static const hl_lang_def_t lang = {
    .name = "c",
    .rules = rules,
    .rule_count = sizeof(rules) / sizeof(rules[0]),
    .default_token = HL_TOKEN_NONE,
    .detect = detect,
    .detect_count = sizeof(detect) / sizeof(detect[0]),
};

const hl_lang_def_t *hl_lang_c(void) { return &lang; }
