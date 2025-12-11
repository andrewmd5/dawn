// dawn_tex_symbols.c

#include "dawn_tex.h"
#include "utf8proc/utf8proc.h"
#include <string.h>
#include <stdlib.h>

// #region Mathematical Alphabets

//! Unicode mathematical alphabets for font styling
//! Each string contains 52 characters: A-Z then a-z

static const char *ALPHABET_NORMAL =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static const char *ALPHABET_SERIF_IT =
    "𝐴𝐵𝐶𝐷𝐸𝐹𝐺𝐻𝐼𝐽𝐾𝐿𝑀𝑁𝑂𝑃𝑄𝑅𝑆𝑇𝑈𝑉𝑊𝑋𝑌𝑍𝑎𝑏𝑐𝑑𝑒𝑓𝑔ℎ𝑖𝑗𝑘𝑙𝑚𝑛𝑜𝑝𝑞𝑟𝑠𝑡𝑢𝑣𝑤𝑥𝑦𝑧";

static const char *ALPHABET_SERIF_BLD =
    "𝐀𝐁𝐂𝐃𝐄𝐅𝐆𝐇𝐈𝐉𝐊𝐋𝐌𝐍𝐎𝐏𝐐𝐑𝐒𝐓𝐔𝐕𝐖𝐗𝐘𝐙𝐚𝐛𝐜𝐝𝐞𝐟𝐠𝐡𝐢𝐣𝐤𝐥𝐦𝐧𝐨𝐩𝐪𝐫𝐬𝐭𝐮𝐯𝐰𝐱𝐲𝐳";

static const char *ALPHABET_SERIF_ITBD =
    "𝑨𝑩𝑪𝑫𝑬𝑭𝑮𝑯𝑰𝑱𝑲𝑳𝑴𝑵𝑶𝑷𝑸𝑹𝑺𝑻𝑼𝑽𝑾𝑿𝒀𝒁𝒂𝒃𝒄𝒅𝒆𝒇𝒈𝒉𝒊𝒋𝒌𝒍𝒎𝒏𝒐𝒑𝒒𝒓𝒔𝒕𝒖𝒗𝒘𝒙𝒚𝒛";

static const char *ALPHABET_SANS =
    "𝖠𝖡𝖢𝖣𝖤𝖥𝖦𝖧𝖨𝖩𝖪𝖫𝖬𝖭𝖮𝖯𝖰𝖱𝖲𝖳𝖴𝖵𝖶𝖷𝖸𝖹𝖺𝖻𝖼𝖽𝖾𝖿𝗀𝗁𝗂𝗃𝗄𝗅𝗆𝗇𝗈𝗉𝗊𝗋𝗌𝗍𝗎𝗏𝗐𝗑𝗒𝗓";

static const char *ALPHABET_SANS_IT =
    "𝘈𝘉𝘊𝘋𝘌𝘍𝘎𝘏𝘐𝘑𝘒𝘓𝘔𝘕𝘖𝘗𝘘𝘙𝘚𝘛𝘜𝘝𝘞𝘟𝘠𝘡𝘢𝘣𝘤𝘥𝘦𝘧𝘨𝘩𝘪𝘫𝘬𝘭𝘮𝘯𝘰𝘱𝘲𝘳𝘴𝘵𝘶𝘷𝘸𝘹𝘺𝘻";

static const char *ALPHABET_SANS_BLD =
    "𝗔𝗕𝗖𝗗𝗘𝗙𝗚𝗛𝗜𝗝𝗞𝗟𝗠𝗡𝗢𝗣𝗤𝗥𝗦𝗧𝗨𝗩𝗪𝗫𝗬𝗭𝗮𝗯𝗰𝗱𝗲𝗳𝗴𝗵𝗶𝗷𝗸𝗹𝗺𝗻𝗼𝗽𝗾𝗿𝘀𝘁𝘂𝘃𝘄𝘅𝘆𝘇";

static const char *ALPHABET_SANS_ITBD =
    "𝘼𝘽𝘾𝘿𝙀𝙁𝙂𝙃𝙄𝙅𝙆𝙇𝙈𝙉𝙊𝙋𝙌𝙍𝙎𝙏𝙐𝙑𝙒𝙓𝙔𝙕𝙖𝙗𝙘𝙙𝙚𝙛𝙜𝙝𝙞𝙟𝙠𝙡𝙢𝙣𝙤𝙥𝙦𝙧𝙨𝙩𝙪𝙫𝙬𝙭𝙮𝙯";

static const char *ALPHABET_MONO =
    "𝙰𝙱𝙲𝙳𝙴𝙵𝙶𝙷𝙸𝙹𝙺𝙻𝙼𝙽𝙾𝙿𝚀𝚁𝚂𝚃𝚄𝚅𝚆𝚇𝚈𝚉𝚊𝚋𝚌𝚍𝚎𝚏𝚐𝚑𝚒𝚓𝚔𝚕𝚖𝚗𝚘𝚙𝚚𝚛𝚜𝚝𝚞𝚟𝚠𝚡𝚢𝚣";

static const char *ALPHABET_CALI_BLD =
    "𝓐𝓑𝓒𝓓𝓔𝓕𝓖𝓗𝓘𝓙𝓚𝓛𝓜𝓝𝓞𝓟𝓠𝓡𝓢𝓣𝓤𝓥𝓦𝓧𝓨𝓩𝓪𝓫𝓬𝓭𝓮𝓯𝓰𝓱𝓲𝓳𝓴𝓵𝓶𝓷𝓸𝓹𝓺𝓻𝓼𝓽𝓾𝓿𝔀𝔁𝔂𝔃";

static const char *ALPHABET_FRAK_BLD =
    "𝕬𝕭𝕮𝕯𝕰𝕱𝕲𝕳𝕴𝕵𝕶𝕷𝕸𝕹𝕺𝕻𝕼𝕽𝕾𝕿𝖀𝖁𝖂𝖃𝖄𝖅𝖆𝖇𝖈𝖉𝖊𝖋𝖌𝖍𝖎𝖏𝖐𝖑𝖒𝖓𝖔𝖕𝖖𝖗𝖘𝖙𝖚𝖛𝖜𝖝𝖞𝖟";

static const char *ALPHABET_DOUBLE =
    "𝔸𝔹ℂ𝔻𝔼𝔽𝔾ℍ𝕀𝕁𝕂𝕃𝕄ℕ𝕆ℙℚℝ𝕊𝕋𝕌𝕍𝕎𝕏𝕐ℤ𝕒𝕓𝕔𝕕𝕖𝕗𝕘𝕙𝕚𝕛𝕜𝕝𝕞𝕟𝕠𝕡𝕢𝕣𝕤𝕥𝕦𝕧𝕨𝕩𝕪𝕫";

//! Get alphabet for a font style
const char *tex_get_alphabet(TexFontStyle style) {
    switch (style) {
        case TEX_FONT_NORMAL:    return ALPHABET_NORMAL;
        case TEX_FONT_SERIF_IT:  return ALPHABET_SERIF_IT;
        case TEX_FONT_SERIF_BLD: return ALPHABET_SERIF_BLD;
        case TEX_FONT_SERIF_ITBD: return ALPHABET_SERIF_ITBD;
        case TEX_FONT_SANS:      return ALPHABET_SANS;
        case TEX_FONT_SANS_IT:   return ALPHABET_SANS_IT;
        case TEX_FONT_SANS_BLD:  return ALPHABET_SANS_BLD;
        case TEX_FONT_SANS_ITBD: return ALPHABET_SANS_ITBD;
        case TEX_FONT_MONO:      return ALPHABET_MONO;
        case TEX_FONT_CALI:      return ALPHABET_CALI_BLD;
        case TEX_FONT_FRAK:      return ALPHABET_FRAK_BLD;
        case TEX_FONT_DOUBLE:    return ALPHABET_DOUBLE;
        default:                 return ALPHABET_NORMAL;
    }
}

// #endregion

// #region Superscript/Subscript

//! Unicode superscript and subscript characters
//! Each entry: { base_char, superscript, subscript }
typedef struct {
    const char *normal;
    const char *super;
    const char *sub;
} ScriptPair;

static const ScriptPair SCRIPT_CHARS[] = {
    { " ", " ", " " },
    { "0", "⁰", "₀" }, { "1", "¹", "₁" }, { "2", "²", "₂" },
    { "3", "³", "₃" }, { "4", "⁴", "₄" }, { "5", "⁵", "₅" },
    { "6", "⁶", "₆" }, { "7", "⁷", "₇" }, { "8", "⁸", "₈" },
    { "9", "⁹", "₉" },
    { "+", "⁺", "₊" }, { "-", "⁻", "₋" }, { "=", "⁼", "₌" },
    { "!", "ꜝ", " " },
    { "(", "⁽", "₍" }, { ")", "⁾", "₎" },
    { "A", "ᴬ", " " }, { "a", "ᵃ", "ₐ" },
    { "B", "ᴮ", " " }, { "b", "ᵇ", " " },
    { "C", "ꟲ", " " }, { "c", "ᶜ", " " },
    { "D", "ᴰ", " " }, { "d", "ᵈ", " " },
    { "E", "ᴱ", " " }, { "e", "ᵉ", "ₑ" },
    { "F", "ᶠ", " " }, { "f", "ᶠ", " " },
    { "G", "ᴳ", " " }, { "g", "ᵍ", " " },
    { "H", "ᴴ", " " }, { "h", "ʰ", "ₕ" },
    { "I", "ᴵ", "ᶦ" }, { "i", "ⁱ", "ᵢ" },
    { "J", "ᴶ", " " }, { "j", "ʲ", "ⱼ" },
    { "K", "ᴷ", " " }, { "k", "ᵏ", "ₖ" },
    { "L", "ᴸ", " " }, { "l", "ˡ", "ₗ" },
    { "M", "ᴹ", " " }, { "m", "ᵐ", "ₘ" },
    { "N", "ᴺ", " " }, { "n", "ⁿ", "ₙ" },
    { "O", "ᴼ", " " }, { "o", "ᵒ", "ₒ" },
    { "P", "ᴾ", " " }, { "p", "ᵖ", "ₚ" },
    { "Q", "ꟴ", " " }, { "q", "𐞥", " " },
    { "R", "ᴿ", " " }, { "r", "ʳ", "ᵣ" },
    { "S", "ˢ", "ₛ" }, { "s", "ˢ", "ₛ" },
    { "T", "ᵀ", " " }, { "t", "ᵗ", "ₜ" },
    { "U", "ᵁ", " " }, { "u", "ᵘ", "ᵤ" },
    { "V", "ⱽ", "ᵥ" }, { "v", "ᵛ", "ᵥ" },
    { "W", "ᵂ", " " }, { "w", "ʷ", " " },
    { "X", "ˣ", "ₓ" }, { "x", "ˣ", "ₓ" },
    { "Y", "𐞲", "ᵧ" }, { "y", "ʸ", "ᵧ" },
    { "Z", "ᶻ", " " }, { "z", "ᶻ", " " },
    // Greek
    { "α", "ᵅ", " " }, { "β", "ᵝ", "ᵦ" }, { "γ", "ᵞ", "ᵧ" },
    { "δ", "ᵟ", " " }, { "ε", "ᵋ", " " }, { "θ", "ᶿ", " " },
    { "ι", "ᶥ", " " }, { "ϕ", "ᶲ", " " }, { "φ", "ᵠ", "ᵩ" },
    { "χ", "ᵡ", "ᵪ" }, { "ρ", " ", "ᵨ" },
    { "/", "ᐟ", " " }, { ".", "·", " " },
    { NULL, NULL, NULL }
};

//! Try to convert a character to superscript
//! @return Superscript string or NULL if not available
const char *tex_to_superscript(const char *c) {
    if (!c) return NULL;
    for (int32_t i = 0; SCRIPT_CHARS[i].normal != NULL; i++) {
        if (strcmp(SCRIPT_CHARS[i].normal, c) == 0) {
            if (SCRIPT_CHARS[i].super[0] != ' ' || c[0] == ' ') {
                return SCRIPT_CHARS[i].super;
            }
            return NULL;
        }
    }
    return NULL;
}

//! Try to convert a character to subscript
//! @return Subscript string or NULL if not available
const char *tex_to_subscript(const char *c) {
    if (!c) return NULL;
    for (int32_t i = 0; SCRIPT_CHARS[i].normal != NULL; i++) {
        if (strcmp(SCRIPT_CHARS[i].normal, c) == 0) {
            if (SCRIPT_CHARS[i].sub[0] != ' ' || c[0] == ' ') {
                return SCRIPT_CHARS[i].sub;
            }
            return NULL;
        }
    }
    return NULL;
}

//! Unshrink a script character back to normal
//! @return Normal character string or NULL if not found
const char *tex_unshrink_char(const char *c) {
    if (!c) return NULL;
    for (int32_t i = 0; SCRIPT_CHARS[i].normal != NULL; i++) {
        if (strcmp(SCRIPT_CHARS[i].super, c) == 0 ||
            strcmp(SCRIPT_CHARS[i].sub, c) == 0) {
            return SCRIPT_CHARS[i].normal;
        }
    }
    return NULL;
}

// #endregion

// #region Math Symbols

typedef struct {
    const char *name;
    const char *symbol;
} TexSymbol;

static const TexSymbol TEX_SYMBOLS[] = {
    // Self-replacement commands
    { "_", "_" }, { "$", "$" }, { "{", "{" }, { "}", "}" },
    { "#", "#" }, { "&", "&" }, { "%", "%" },

    // Spacing
    { " ", " " }, { ";", " " }, { ":", " " }, { ">", " " },
    { ",", " " }, { "!", "" },
    { "quad", "  " }, { "qquad", "    " },

    // Math functions
    { "arccos", "arccos" }, { "arcsin", "arcsin" }, { "arctan", "arctan" },
    { "arg", "arg" }, { "cos", "cos" }, { "cosh", "cosh" },
    { "cot", "cot" }, { "coth", "coth" }, { "csc", "csc" },
    { "deg", "deg" }, { "det", "det" }, { "dim", "dim" },
    { "exp", "exp" }, { "gcd", "gcd" }, { "hom", "hom" },
    { "inf", "inf" }, { "ker", "ker" }, { "lg", "lg" },
    { "lim", "lim" }, { "liminf", "liminf" }, { "limsup", "limsup" },
    { "ln", "ln" }, { "log", "log" }, { "max", "max" },
    { "min", "min" }, { "Pr", "Pr" }, { "sec", "sec" },
    { "sin", "sin" }, { "sinh", "sinh" }, { "sup", "sup" },
    { "tan", "tan" }, { "tanh", "tanh" },
    { "bmod", "bmod" }, { "pmod", "pmod" }, { "mod", "  mod" },

    // Greek letters
    { "alpha", "α" }, { "beta", "β" }, { "gamma", "γ" }, { "delta", "δ" },
    { "epsilon", "ϵ" }, { "varepsilon", "ε" }, { "zeta", "ζ" },
    { "eta", "η" }, { "theta", "θ" }, { "vartheta", "ϑ" },
    { "iota", "ι" }, { "kappa", "κ" }, { "lambda", "λ" },
    { "mu", "μ" }, { "nu", "ν" }, { "xi", "ξ" },
    { "pi", "π" }, { "varpi", "ϖ" }, { "rho", "ρ" }, { "varrho", "ϱ" },
    { "sigma", "σ" }, { "varsigma", "ς" }, { "tau", "τ" },
    { "upsilon", "υ" }, { "phi", "ϕ" }, { "varphi", "φ" },
    { "chi", "χ" }, { "psi", "ψ" }, { "omega", "ω" },
    // Capital Greek
    { "Gamma", "Γ" }, { "Delta", "Δ" }, { "Theta", "Θ" },
    { "Lambda", "Λ" }, { "Xi", "Ξ" }, { "Pi", "Π" },
    { "Sigma", "Σ" }, { "Upsilon", "Υ" }, { "Phi", "Φ" },
    { "Psi", "Ψ" }, { "Omega", "Ω" },

    // Binary operators
    { "pm", "±" }, { "mp", "∓" }, { "times", "×" }, { "div", "÷" },
    { "cdot", "⋅" }, { "ast", "∗" }, { "star", "⋆" }, { "circ", "∘" },
    { "bullet", "•" }, { "cap", "∩" }, { "cup", "∪" },
    { "sqcap", "⊓" }, { "sqcup", "⊔" }, { "vee", "∨" }, { "wedge", "∧" },
    { "setminus", "⧵" }, { "wr", "≀" }, { "diamond", "⋄" },
    { "bigtriangleup", "△" }, { "bigtriangledown", "▽" },
    { "triangleleft", "◁" }, { "triangleright", "▷" },
    { "oplus", "⊕" }, { "ominus", "⊖" }, { "otimes", "⊗" },
    { "oslash", "⊘" }, { "odot", "⊙" }, { "bigcirc", "◯" },
    { "dagger", "†" }, { "ddagger", "‡" }, { "amalg", "⨿" },
    { "boxtimes", "⊠" },

    // Relations
    { "le", "≤" }, { "leq", "≤" }, { "ge", "≥" }, { "geq", "≥" },
    { "ne", "≠" }, { "neq", "≠" }, { "equiv", "≡" },
    { "ll", "≪" }, { "gg", "≫" }, { "doteq", "≐" },
    { "prec", "≺" }, { "succ", "≻" }, { "preceq", "⪯" }, { "succeq", "⪰" },
    { "sim", "∼" }, { "simeq", "≃" }, { "asymp", "≍" },
    { "approx", "≈" }, { "cong", "≅" }, { "propto", "∝" },
    { "subset", "⊂" }, { "supset", "⊃" },
    { "subseteq", "⊆" }, { "supseteq", "⊇" },
    { "sqsubset", "⊏" }, { "sqsupset", "⊐" },
    { "sqsubseteq", "⊑" }, { "sqsupseteq", "⊒" },
    { "in", "∈" }, { "ni", "∋" }, { "notin", "∉" }, { "owns", "∋" },
    { "vdash", "⊢" }, { "dashv", "⊣" }, { "models", "⊨" },
    { "perp", "⟂" }, { "mid", "∣" }, { "parallel", "∥" },
    { "bowtie", "⋈" }, { "Join", "⨝" }, { "smile", "⌣" }, { "frown", "⌢" },

    // Arrows
    { "leftarrow", "←" }, { "gets", "←" },
    { "rightarrow", "→" }, { "to", "→" },
    { "leftrightarrow", "↔" },
    { "Leftarrow", "⇐" }, { "Rightarrow", "⇒" }, { "Leftrightarrow", "⇔" },
    { "mapsto", "↦" }, { "longmapsto", "⟼ " },
    { "hookleftarrow", "↩" }, { "hookrightarrow", "↪" },
    { "leftharpoonup", "↼" }, { "leftharpoondown", "↽" },
    { "rightharpoonup", "⇀" }, { "rightharpoondown", "⇁" },
    { "rightleftharpoons", "⇌" },
    { "longleftarrow", "⟵ " }, { "longrightarrow", "⟶ " },
    { "longleftrightarrow", "⟷ " },
    { "uparrow", "↑" }, { "downarrow", "↓" }, { "updownarrow", "↕" },
    { "Uparrow", "⇑" }, { "Downarrow", "⇓" }, { "Updownarrow", "⇕" },
    { "nearrow", "↗" }, { "searrow", "↘" },
    { "swarrow", "↙" }, { "nwarrow", "↖" },
    { "leadsto", "⇝" }, { "iff", "⟷ " },

    // Miscellaneous
    { "aleph", "ℵ" }, { "hbar", "ℏ" }, { "ell", "ℓ" },
    { "wp", "℘" }, { "Re", "ℜ" }, { "Im", "ℑ" },
    { "partial", "∂" }, { "infty", "∞" }, { "prime", "′" },
    { "emptyset", "∅" }, { "vanothing", "∅" }, { "nabla", "∇" },
    { "surd", "√" }, { "top", "⊤" }, { "bot", "⊥" },
    { "angle", "∠" }, { "triangle", "△" },
    { "forall", "∀" }, { "exists", "∃" }, { "neg", "¬" }, { "lnot", "¬" },
    { "flat", "♭" }, { "natural", "♮" }, { "sharp", "♯" },
    { "clubsuit", "♣" }, { "diamondsuit", "♢" },
    { "heartsuit", "♡" }, { "spadesuit", "♠" },
    { "Box", "□" }, { "Diamond", "◇" },
    { "imath", "ı" }, { "jmath", "ȷ" },
    { "complement", "∁" }, { "mho", "℧" },

    // Delimiters
    { "langle", "⟨" }, { "rangle", "⟩" },
    { "lbrace", "{" }, { "rbrace", "}" },
    { "lbrack", "[" }, { "rbrack", "]" },
    { "lceil", "⌈" }, { "rceil", "⌉" },
    { "lfloor", "⌊" }, { "rfloor", "⌋" },
    { "lvert", "|" }, { "rvert", "|" },
    { "vert", "|" }, { "Vert", "‖" }, { "|", "∥" },
    { "backslash", "\\" },

    // Large operators (single-char forms)
    { "sum", "∑" }, { "prod", "∏" }, { "coprod", "∐" },
    { "int32_t", "∫" }, { "oint", "∮" }, { "smallint", "∫" },

    // Dots
    { "cdots", "⋯" }, { "dots", "…" }, { "ldots", "…" },
    { "vdots", "⋮" }, { "ddots", "⋱" },

    // Logic
    { "land", "∧" }, { "lor", "∨" }, { "not", "⧸" },
    { "because", "∵" }, { "therefore", "∴" },
    { "divides", "∣" },

    // Special
    { "LaTeX", "LᴬTₑX" },
    { "TeXicode", "TᵉXᵢcₒdₑ" },
    { "restriction", "↾" }, { "upharpoonright", "↾" },
    { "revemptyset", "⦰" },
    { "lhd", "◁" }, { "rhd", "◁" },
    { "unlhd", "⊴" }, { "unrhd", "⊵" },
    { "trianglelefteq", "⊴" },
    { "uplus", "⊎" },
    { "mathdollar", "$" }, { "mathparagraph", "¶" },
    { "mathsection", "§" }, { "mathsterling", "£" },
    { "mathunderscore", "_" },

    { NULL, NULL }
};

//! Look up a command symbol
//! @return Symbol string or NULL if not found
const char *tex_lookup_symbol(const char *name) {
    for (int32_t i = 0; TEX_SYMBOLS[i].name != NULL; i++) {
        if (strcmp(TEX_SYMBOLS[i].name, name) == 0) {
            return TEX_SYMBOLS[i].symbol;
        }
    }
    return NULL;
}

// #endregion

// #region Multi-line Operators

//! Multi-line operator data
//! Format: concatenated rows, each row has 'width' characters
typedef struct {
    const char *name;
    const char *art;      //! Concatenated row data
    int32_t height;
    int32_t width;
    int32_t horizon;
} TexMultilineOp;

static const TexMultilineOp TEX_MULTILINE_OPS[] = {
    { "sum",   "┰─╴▐╸ ┸─╴", 3, 3, 1 },
    { "prod",  "┰─┰┃ ┃┸ ┸", 3, 3, 1 },
    { "int32_t",   "⌠│⌡", 3, 1, 1 },
    { "iint",  "⌠⌠││⌡⌡", 3, 2, 1 },
    { "iiint", "⌠⌠⌠│││⌡⌡⌡", 3, 3, 1 },
    { "idotsint", "⌠ ⌠│⋯│⌡ ⌡", 3, 3, 1 },
    { "oint",  " ⌠ ╶╪╴ ⌡ ", 3, 3, 1 },
    { "oiint", " ⌠⌠ ╶╪╪╴ ⌡⌡ ", 3, 4, 1 },
    { "oiiint", " ⌠⌠⌠ ╺╪╪╪╸ ⌡⌡⌡ ", 3, 5, 1 },
    { NULL, NULL, 0, 0, 0 }
};

//! Get multi-line operator art
//! @return Concatenated row string or NULL if not found
const char *tex_get_multiline_op(const char *name, int32_t *out_height, int32_t *out_width, int32_t *out_horizon) {
    for (int32_t i = 0; TEX_MULTILINE_OPS[i].name != NULL; i++) {
        if (strcmp(TEX_MULTILINE_OPS[i].name, name) == 0) {
            *out_height = TEX_MULTILINE_OPS[i].height;
            *out_width = TEX_MULTILINE_OPS[i].width;
            *out_horizon = TEX_MULTILINE_OPS[i].horizon;
            return TEX_MULTILINE_OPS[i].art;
        }
    }
    return NULL;
}

// #endregion

// #region Delimiters

//! Delimiter character lookup
//! Format: sgl (single), top, ctr (center), fil (fill), btm (bottom)
static const char *DELIMITER_SGL = "(){}[]⌊⌋⌈⌉||‖‖";
static const char *DELIMITER_TOP = "⎛⎞⎧⎫⎡⎤⎢⎥⎡⎤⎟⎜║║";
static const char *DELIMITER_CTR = "⎜⎟⎨⎬⎢⎥⎢⎥⎢⎥⎟⎜║║";
static const char *DELIMITER_FIL = "⎜⎟⎪⎪⎢⎥⎢⎥⎢⎥⎟⎜║║";
static const char *DELIMITER_BTM = "⎝⎠⎩⎭⎣⎦⎣⎦⎢⎥⎟⎜║║";

//! Find character index in delimiter string
static int32_t find_delim_index(char c) {
    const char *p = DELIMITER_SGL;
    int32_t idx = 0;
    while (*p) {
        int32_t len = utf8proc_utf8class[(uint8_t)*p];
        if (len < 1) len = 1;

        // Compare single byte for ASCII
        if (len == 1 && *p == c) {
            return idx;
        }
        p += len;
        idx++;
    }
    return -1;
}

//! Get UTF-8 char at index
static const char *get_utf8_at_index(const char *s, int32_t idx) {
    static char buf[8];
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

//! Get delimiter character for position
//! @param delim Single delimiter character
//! @param position TexDelimPos enum value
const char *tex_get_delimiter_char(char delim, TexDelimPos position) {
    int32_t idx = find_delim_index(delim);
    if (idx < 0) return NULL;

    const char *lookup = NULL;
    switch (position) {
        case TEX_DELIM_SGL: lookup = DELIMITER_SGL; break;
        case TEX_DELIM_TOP: lookup = DELIMITER_TOP; break;
        case TEX_DELIM_CTR: lookup = DELIMITER_CTR; break;
        case TEX_DELIM_FIL: lookup = DELIMITER_FIL; break;
        case TEX_DELIM_BTM: lookup = DELIMITER_BTM; break;
    }

    return get_utf8_at_index(lookup, idx);
}

// #endregion

// #region Accent Combining Characters

typedef struct {
    const char *name;
    const char *combining;
} TexAccent;

static const TexAccent TEX_ACCENTS[] = {
    { "acute", "\u0302" },      // Note: Python has 0302 for acute, which is actually circumflex
    { "bar", "\u0304" },
    { "breve", "\u0306" },
    { "check", "\u030C" },
    { "ddot", "\u0308" },
    { "dot", "\u0307" },
    { "grave", "\u0300" },
    { "hat", "\u0302" },
    { "mathring", "\u030A" },
    { "tilde", "\u0303" },
    { "vec", "\u20D7" },
    { "widehat", "\u0302" },
    { "widetilde", "\u0360" },
    { NULL, NULL }
};

//! Get accent combining character
const char *tex_get_accent(const char *name) {
    for (int32_t i = 0; TEX_ACCENTS[i].name != NULL; i++) {
        if (strcmp(TEX_ACCENTS[i].name, name) == 0) {
            return TEX_ACCENTS[i].combining;
        }
    }
    return NULL;
}

// #endregion

// #region Font Reversion

//! Search a single alphabet string for a character, return ASCII if found
static char search_alphabet(const char *alphabet, const char *ch) {
    const uint8_t *alpha = (const uint8_t *)alphabet;
    const uint8_t *target = (const uint8_t *)ch;
    size_t alpha_len = strlen(alphabet);
    size_t target_len = strlen(ch);

    if (target_len == 0) return 0;

    size_t pos = 0;
    int32_t index = 0;

    while (pos < alpha_len) {
        int32_t len = utf8proc_utf8class[alpha[pos]];
        if (len < 1) len = 1;

        if ((size_t)len == target_len && memcmp(alpha + pos, target, len) == 0) {
            // Found! Index 0-25 = A-Z, 26-51 = a-z
            if (index < 26) return 'A' + index;
            return 'a' + (index - 26);
        }

        pos += len;
        index++;
    }
    return 0;
}

//! Revert a styled character back to ASCII
//! @return ASCII character or 0 if not found
char tex_revert_font_char(const char *ch) {
    if (!ch || !*ch) return 0;

    // ASCII passthrough
    if ((uint8_t)ch[0] < 128) {
        return ch[0];
    }

    // Search all mathematical alphabets
    char result;
    if ((result = search_alphabet(ALPHABET_SERIF_IT, ch))) return result;
    if ((result = search_alphabet(ALPHABET_SERIF_BLD, ch))) return result;
    if ((result = search_alphabet(ALPHABET_SERIF_ITBD, ch))) return result;
    if ((result = search_alphabet(ALPHABET_SANS, ch))) return result;
    if ((result = search_alphabet(ALPHABET_SANS_IT, ch))) return result;
    if ((result = search_alphabet(ALPHABET_SANS_BLD, ch))) return result;
    if ((result = search_alphabet(ALPHABET_SANS_ITBD, ch))) return result;
    if ((result = search_alphabet(ALPHABET_MONO, ch))) return result;
    if ((result = search_alphabet(ALPHABET_CALI_BLD, ch))) return result;
    if ((result = search_alphabet(ALPHABET_FRAK_BLD, ch))) return result;
    if ((result = search_alphabet(ALPHABET_DOUBLE, ch))) return result;

    return 0;
}

// #endregion

// #region Font Command Mapping

typedef struct {
    const char *name;
    TexFontStyle style;
} TexFontCmd;

static const TexFontCmd TEX_FONT_CMDS[] = {
    { "mathrm", TEX_FONT_NORMAL },
    { "mathbf", TEX_FONT_SERIF_BLD },
    { "mathsf", TEX_FONT_SANS },
    { "mathtt", TEX_FONT_MONO },
    { "mathit", TEX_FONT_SERIF_IT },
    { "mathnormal", TEX_FONT_SERIF_IT },
    { "mathcal", TEX_FONT_CALI },
    { "mathscr", TEX_FONT_CALI },
    { "mathfrak", TEX_FONT_FRAK },
    { "mathbb", TEX_FONT_DOUBLE },
    { "text", TEX_FONT_NORMAL },
    { NULL, TEX_FONT_NORMAL }
};

//! Get font style for a command
TexFontStyle tex_get_font_style(const char *name) {
    for (int32_t i = 0; TEX_FONT_CMDS[i].name != NULL; i++) {
        if (strcmp(TEX_FONT_CMDS[i].name, name) == 0) {
            return TEX_FONT_CMDS[i].style;
        }
    }
    return TEX_FONT_NORMAL;
}

// #endregion

// #region Command Type Lookup

typedef struct {
    const char *cmd;
    TexNodeType type;
} TexCmdType;

static const TexCmdType CMD_TYPES[] = {
    // Math mode delimiters
    { "[", TEX_NT_OPN_BRAK },
    { "]", TEX_NT_CLS_BRAK },
    { "(", TEX_NT_OPN_PREN },
    { ")", TEX_NT_CLS_PREN },

    // Commands
    { "sqrt", TEX_NT_CMD_SQRT },
    { "frac", TEX_NT_CMD_FRAC },
    { "tfrac", TEX_NT_CMD_FRAC },
    { "dfrac", TEX_NT_CMD_FRAC },
    { "cfrac", TEX_NT_CMD_FRAC },
    { "binom", TEX_NT_CMD_BINOM },
    { "dbinom", TEX_NT_CMD_BINOM },
    { "tbinom", TEX_NT_CMD_BINOM },
    { "text", TEX_NT_CMD_TEXT },
    { "textrm", TEX_NT_CMD_TEXT },
    { "textit", TEX_NT_CMD_TEXT },
    { "textbf", TEX_NT_CMD_TEXT },
    { "texttt", TEX_NT_CMD_TEXT },
    { "textsf", TEX_NT_CMD_TEXT },
    { "mbox", TEX_NT_CMD_TEXT },
    { "hbox", TEX_NT_CMD_TEXT },
    { "substack", TEX_NT_CMD_SBSTK },
    { "begin", TEX_NT_CMD_BGIN },
    { "end", TEX_NT_CMD_END },
    { "\\", TEX_NT_CMD_LBRK },
    { "newline", TEX_NT_CMD_LBRK },
    { "limits", TEX_NT_CMD_LMTS },
    { "nolimits", TEX_NT_CMD_LMTS },
    { "left", TEX_NT_OPN_DLIM },
    { "right", TEX_NT_CLS_DLIM },

    // Big delimiters
    { "big", TEX_NT_BIG_DLIM },
    { "Big", TEX_NT_BIG_DLIM },
    { "bigg", TEX_NT_BIG_DLIM },
    { "Bigg", TEX_NT_BIG_DLIM },
    { "bigl", TEX_NT_BIG_DLIM },
    { "Bigl", TEX_NT_BIG_DLIM },
    { "biggl", TEX_NT_BIG_DLIM },
    { "Biggl", TEX_NT_BIG_DLIM },
    { "bigr", TEX_NT_BIG_DLIM },
    { "Bigr", TEX_NT_BIG_DLIM },
    { "biggr", TEX_NT_BIG_DLIM },
    { "Biggr", TEX_NT_BIG_DLIM },
    { "bigm", TEX_NT_BIG_DLIM },
    { "Bigm", TEX_NT_BIG_DLIM },
    { "biggm", TEX_NT_BIG_DLIM },
    { "Biggm", TEX_NT_BIG_DLIM },

    // Style commands
    { "displaystyle", TEX_NT_CMD_STYL },
    { "textstyle", TEX_NT_CMD_STYL },
    { "scriptstyle", TEX_NT_CMD_STYL },
    { "scriptscriptstyle", TEX_NT_CMD_STYL },

    // Center-base operators
    { "sum", TEX_NT_CTR_BASE },
    { "prod", TEX_NT_CTR_BASE },
    { "coprod", TEX_NT_CTR_BASE },
    { "int32_t", TEX_NT_CTR_BASE },
    { "iint", TEX_NT_CTR_BASE },
    { "iiint", TEX_NT_CTR_BASE },
    { "oint", TEX_NT_CTR_BASE },
    { "bigcup", TEX_NT_CTR_BASE },
    { "bigcap", TEX_NT_CTR_BASE },
    { "bigvee", TEX_NT_CTR_BASE },
    { "bigwedge", TEX_NT_CTR_BASE },
    { "bigoplus", TEX_NT_CTR_BASE },
    { "bigotimes", TEX_NT_CTR_BASE },
    { "bigsqcup", TEX_NT_CTR_BASE },
    { "biguplus", TEX_NT_CTR_BASE },
    { "lim", TEX_NT_CTR_BASE },
    { "limsup", TEX_NT_CTR_BASE },
    { "liminf", TEX_NT_CTR_BASE },
    { "max", TEX_NT_CTR_BASE },
    { "min", TEX_NT_CTR_BASE },
    { "sup", TEX_NT_CTR_BASE },
    { "inf", TEX_NT_CTR_BASE },
    { "det", TEX_NT_CTR_BASE },
    { "Pr", TEX_NT_CTR_BASE },
    { "gcd", TEX_NT_CTR_BASE },

    { NULL, TEX_NT_NONE }
};

//! Look up node type for a command
TexNodeType tex_lookup_cmd_type(const char *cmd) {
    if (!cmd) return TEX_NT_NONE;

    // Check font commands first
    for (int32_t i = 0; TEX_FONT_CMDS[i].name != NULL; i++) {
        if (strcmp(TEX_FONT_CMDS[i].name, cmd) == 0) {
            return TEX_NT_CMD_FONT;
        }
    }

    // Check accent commands
    for (int32_t i = 0; TEX_ACCENTS[i].name != NULL; i++) {
        if (strcmp(TEX_ACCENTS[i].name, cmd) == 0) {
            return TEX_NT_CMD_ACNT;
        }
    }

    // Check command type table
    for (int32_t i = 0; CMD_TYPES[i].cmd != NULL; i++) {
        if (strcmp(CMD_TYPES[i].cmd, cmd) == 0) {
            return CMD_TYPES[i].type;
        }
    }

    return TEX_NT_NONE;
}

// #endregion

// #region Parent-Dependent Type Lookup

typedef struct {
    TexNodeType parent;
    TexTokenType tok_type;
    const char *value;
    TexNodeType result;
} TexParentDepType;

static const TexParentDepType PARENT_DEP_TYPES[] = {
    // OPN_DLIM accepts [ as TXT_LEAF (not OPN_DEGR)
    { TEX_NT_OPN_DLIM, TEX_TOK_SYMB, "[", TEX_NT_TXT_LEAF },
    { TEX_NT_OPN_DLIM, TEX_TOK_SYMB, "]", TEX_NT_TXT_LEAF },

    // CMD_SQRT: [ opens degree
    { TEX_NT_CMD_SQRT, TEX_TOK_SYMB, "[", TEX_NT_OPN_DEGR },

    // OPN_DEGR: ] closes degree
    { TEX_NT_OPN_DEGR, TEX_TOK_SYMB, "]", TEX_NT_CLS_DEGR },

    // Meta tokens
    { TEX_NT_OPN_ROOT, TEX_TOK_META, "end", TEX_NT_CLS_ROOT },
    { TEX_NT_OPN_ROOT, TEX_TOK_META, "startline", TEX_NT_OPN_LINE },
    { TEX_NT_OPN_LINE, TEX_TOK_META, "endline", TEX_NT_CLS_LINE },

    // CMD_BGIN: { opens environment name
    { TEX_NT_CMD_BGIN, TEX_TOK_SYMB, "{", TEX_NT_OPN_ENVN },

    // OPN_ENVN: } closes environment name
    { TEX_NT_OPN_ENVN, TEX_TOK_SYMB, "}", TEX_NT_CLS_ENVN },

    // CMD_TEXT: { opens text mode
    { TEX_NT_CMD_TEXT, TEX_TOK_SYMB, "{", TEX_NT_OPN_TEXT },

    // OPN_TEXT: } closes text mode
    { TEX_NT_OPN_TEXT, TEX_TOK_SYMB, "}", TEX_NT_CLS_TEXT },

    // CMD_SBSTK: { opens substack line
    { TEX_NT_CMD_SBSTK, TEX_TOK_SYMB, "{", TEX_NT_OPN_STKLN },

    // OPN_STKLN: \\ creates new substack line, } closes
    { TEX_NT_OPN_STKLN, TEX_TOK_SYMB, "}", TEX_NT_CLS_STKLN },

    { TEX_NT_NONE, TEX_TOK_NONE, NULL, TEX_NT_NONE }
};

//! Get node type based on parent context
TexNodeType tex_get_parent_dep_type(TexNodeType parent, TexTokenType tok_type, const char *value) {
    if (!value) return TEX_NT_NONE;

    for (int32_t i = 0; PARENT_DEP_TYPES[i].value != NULL; i++) {
        if (PARENT_DEP_TYPES[i].parent == parent &&
            PARENT_DEP_TYPES[i].tok_type == tok_type &&
            strcmp(PARENT_DEP_TYPES[i].value, value) == 0) {
            return PARENT_DEP_TYPES[i].result;
        }
    }

    return TEX_NT_NONE;
}

// #endregion
