//! @file lang/rust.c
//! @brief Rust language definition

#include "../highlight.h"

static const hl_lang_rule_t rules[] = {
    { .pattern = "//.*(?:\\n|$)|/\\*(?:(?!\\*/).|[\\s\\S])*(?:\\*/)?", .token = HL_TOKEN_CMNT },
    { .pattern = "([\"'])(?:\\\\[\\s\\S]|(?!\\1)[^\\r\\n\\\\])*\\1?", .token = HL_TOKEN_STR },
    { .pattern = "r#*\"[\\s\\S]*?\"#*", .token = HL_TOKEN_STR },
    { .pattern = "b\"(?:\\\\[\\s\\S]|[^\"])*\"", .token = HL_TOKEN_STR },
    { .pattern = "b'(?:\\\\[\\s\\S]|[^'])'", .token = HL_TOKEN_STR },
    { .pattern = "(?:\\.e?|\\b)\\d(?:e-|[\\d.oxa-fA-F_])*(?:_?(?:i8|i16|i32|i64|i128|isize|u8|u16|u32|u64|u128|usize|f32|f64))?(?:\\.|\\b)", .token = HL_TOKEN_NUM },
    // Keywords (non-type)
    { .pattern = "\\b(?:as|break|const|continue|crate|else|enum|extern|fn|for|if|impl|in|let|loop|match|mod|move|mut|pub|ref|return|self|Self|static|struct|super|trait|type|unsafe|use|where|while|async|await|dyn|box|try|yield|macro_rules)\\b", .token = HL_TOKEN_KWD },
    // Primitive types
    { .pattern = "\\b(?:bool|char|str|i8|i16|i32|i64|i128|isize|u8|u16|u32|u64|u128|usize|f32|f64)\\b", .token = HL_TOKEN_TYPE },
    // Common std types
    { .pattern = "\\b(?:String|Vec|Box|Rc|Arc|Cell|RefCell|Mutex|RwLock|Option|Result|Some|None|Ok|Err)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:HashMap|HashSet|BTreeMap|BTreeSet|VecDeque|LinkedList|BinaryHeap)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:Path|PathBuf|OsStr|OsString|CStr|CString)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:File|Read|Write|Seek|BufRead|BufReader|BufWriter)\\b", .token = HL_TOKEN_TYPE },
    { .pattern = "\\b(?:Iterator|IntoIterator|FromIterator|Extend|Clone|Copy|Send|Sync|Sized|Default|Debug|Display|From|Into|TryFrom|TryInto|AsRef|AsMut|Deref|DerefMut|Drop|Fn|FnMut|FnOnce)\\b", .token = HL_TOKEN_TYPE },
    // Boolean
    { .pattern = "\\b(?:true|false)\\b", .token = HL_TOKEN_BOOL },
    // Operators
    { .pattern = "[/*+:?&|%^~=!,<>.^-]+", .token = HL_TOKEN_OPER },
    { .pattern = "\\b[A-Z][\\w_]*\\b", .token = HL_TOKEN_CLASS },
    // Macros and function calls
    { .pattern = "[a-zA-Z_][\\w_]*!", .token = HL_TOKEN_FUNC },
    { .pattern = "[a-zA-Z_][\\w_]*(?=\\s*\\()", .token = HL_TOKEN_FUNC },
    // Lifetime annotations
    { .pattern = "'[a-zA-Z_][\\w_]*", .token = HL_TOKEN_TYPE },
    // Attributes
    { .pattern = "#!?\\[[^\\]]*\\]", .token = HL_TOKEN_FUNC },
};

static const hl_detect_rule_t detect[] = {
    { "^\\s*(use|fn|mut|match)\\b", 100 },
};

static const hl_lang_def_t lang = {
    .name = "rust",
    .rules = rules,
    .rule_count = sizeof(rules) / sizeof(rules[0]),
    .default_token = HL_TOKEN_NONE,
    .detect = detect,
    .detect_count = sizeof(detect) / sizeof(detect[0]),
};

const hl_lang_def_t *hl_lang_rust(void) { return &lang; }
