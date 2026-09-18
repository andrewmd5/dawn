#include "dawn_theme.h"

// #region DawnColor Palettes

typedef struct {
    const char* name;
    bool dark;
    DawnColor bg;
    DawnColor fg;
    DawnColor dim;
    DawnColor accent;
    DawnColor select;
    DawnColor ai_bg;
    DawnColor border;
    DawnColor code_bg;
    DawnColor modal_bg;
    DawnColor headings[6];
} ThemePalette;

static const ThemePalette THEMES[THEME_COUNT] = {
    {
            "ayu-light", false,
            { 250, 250, 247 }, { 91, 96, 93 }, { 155, 159, 153 }, { 234, 132, 68 },
            { 229, 229, 224 }, { 244, 244, 239 }, { 213, 214, 208 }, { 239, 239, 234 }, { 253, 253, 250 },
            { { 158, 89, 46 }, { 158, 46, 57 }, { 119, 158, 46 }, { 46, 158, 87 }, { 158, 143, 46 }, { 91, 96, 93 } }
        },
    {
            "catppuccin-latte", false,
            { 239, 241, 245 }, { 76, 79, 105 }, { 145, 148, 170 }, { 136, 57, 239 },
            { 220, 222, 229 }, { 245, 245, 247 }, { 204, 207, 217 }, { 231, 233, 239 }, { 249, 249, 251 },
            { { 90, 38, 158 }, { 38, 43, 158 }, { 158, 38, 111 }, { 158, 88, 38 }, { 148, 38, 158 }, { 76, 79, 105 } }
        },
    {
            "ice", false,
            { 235, 245, 250 }, { 42, 62, 72 }, { 124, 151, 164 }, { 55, 150, 180 },
            { 211, 229, 238 }, { 226, 239, 246 }, { 196, 219, 229 }, { 221, 236, 243 }, { 247, 252, 254 },
            { { 48, 132, 158 }, { 48, 158, 132 }, { 70, 48, 158 }, { 158, 48, 134 }, { 48, 79, 158 }, { 42, 62, 72 } }
        },
    {
            "light", false,
            { 252, 250, 245 }, { 45, 45, 45 }, { 160, 155, 145 }, { 120, 100, 80 },
            { 255, 245, 200 }, { 245, 243, 238 }, { 220, 215, 205 }, { 240, 238, 233 }, { 255, 253, 250 },
            { { 158, 115, 71 }, { 158, 73, 71 }, { 118, 158, 71 }, { 71, 158, 113 }, { 158, 156, 71 }, { 45, 45, 45 } }
        },
    {
            "lilac", false,
            { 238, 231, 246 }, { 67, 57, 78 }, { 143, 128, 157 }, { 139, 92, 191 },
            { 219, 205, 231 }, { 244, 238, 248 }, { 206, 190, 220 }, { 230, 219, 239 }, { 249, 245, 251 },
            { { 112, 71, 158 }, { 71, 72, 158 }, { 158, 71, 120 }, { 158, 111, 71 }, { 154, 71, 158 }, { 67, 57, 78 } }
        },
    {
            "one-light", false,
            { 250, 250, 250 }, { 56, 58, 66 }, { 154, 158, 169 }, { 64, 120, 180 },
            { 229, 231, 235 }, { 244, 245, 247 }, { 218, 220, 225 }, { 243, 244, 246 }, { 255, 255, 255 },
            { { 56, 105, 158 }, { 56, 154, 158 }, { 105, 56, 158 }, { 158, 56, 107 }, { 56, 56, 158 }, { 56, 58, 66 } }
        },
    {
            "paper", false,
            { 248, 247, 242 }, { 48, 49, 46 }, { 157, 157, 150 }, { 78, 112, 86 },
            { 231, 231, 224 }, { 243, 242, 235 }, { 213, 214, 206 }, { 238, 238, 231 }, { 253, 252, 248 },
            { { 71, 158, 92 }, { 92, 158, 71 }, { 71, 141, 158 }, { 90, 71, 158 }, { 71, 158, 133 }, { 48, 49, 46 } }
        },
    {
            "sakura", false,
            { 252, 239, 244 }, { 75, 53, 64 }, { 157, 126, 139 }, { 214, 93, 137 },
            { 239, 210, 222 }, { 248, 231, 239 }, { 225, 198, 211 }, { 245, 222, 232 }, { 255, 247, 251 },
            { { 158, 69, 101 }, { 158, 69, 144 }, { 158, 122, 69 }, { 103, 158, 69 }, { 158, 79, 69 }, { 75, 53, 64 } }
        },
    {
            "sepia", false,
            { 244, 235, 214 }, { 67, 52, 38 }, { 145, 123, 96 }, { 137, 88, 51 },
            { 226, 204, 163 }, { 239, 227, 203 }, { 211, 193, 163 }, { 235, 222, 194 }, { 248, 239, 220 },
            { { 158, 102, 59 }, { 158, 59, 64 }, { 119, 158, 59 }, { 59, 158, 100 }, { 158, 149, 59 }, { 67, 52, 38 } }
        },
    {
            "solarized-light", false,
            { 253, 246, 227 }, { 101, 123, 131 }, { 147, 161, 161 }, { 38, 139, 210 },
            { 238, 232, 213 }, { 246, 239, 218 }, { 224, 216, 194 }, { 246, 239, 218 }, { 255, 250, 235 },
            { { 29, 105, 158 }, { 29, 158, 149 }, { 77, 29, 158 }, { 158, 29, 107 }, { 29, 42, 158 }, { 101, 123, 131 } }
        },
    {
            "amber", true,
            { 42, 32, 10 }, { 242, 226, 176 }, { 145, 127, 73 }, { 245, 190, 40 },
            { 79, 63, 23 }, { 54, 42, 14 }, { 91, 75, 29 }, { 49, 38, 12 }, { 63, 51, 18 },
            { { 242, 188, 40 }, { 242, 91, 40 }, { 102, 242, 40 }, { 40, 242, 184 }, { 199, 242, 40 }, { 242, 226, 176 } }
        },
    {
            "ayu-dark", true,
            { 15, 20, 24 }, { 203, 207, 201 }, { 95, 104, 105 }, { 255, 180, 84 },
            { 37, 45, 49 }, { 22, 28, 32 }, { 55, 65, 68 }, { 20, 26, 30 }, { 30, 38, 42 },
            { { 242, 171, 80 }, { 242, 93, 80 }, { 158, 242, 80 }, { 80, 242, 168 }, { 236, 242, 80 }, { 203, 207, 201 } }
        },
    {
            "azure", true,
            { 18, 29, 48 }, { 211, 225, 246 }, { 97, 116, 145 }, { 82, 145, 255 },
            { 38, 56, 82 }, { 26, 40, 63 }, { 56, 75, 104 }, { 23, 36, 58 }, { 32, 48, 73 },
            { { 78, 138, 242 }, { 78, 217, 242 }, { 176, 78, 242 }, { 242, 78, 141 }, { 97, 78, 242 }, { 211, 225, 246 } }
        },
    {
            "catppuccin-mocha", true,
            { 30, 30, 46 }, { 205, 214, 244 }, { 127, 132, 151 }, { 203, 166, 247 },
            { 69, 71, 90 }, { 36, 36, 54 }, { 69, 71, 90 }, { 42, 42, 62 }, { 49, 50, 68 },
            { { 170, 109, 242 }, { 109, 112, 242 }, { 242, 109, 187 }, { 242, 167, 109 }, { 234, 109, 242 }, { 205, 214, 244 } }
        },
    {
            "chocolate", true,
            { 39, 25, 19 }, { 235, 214, 198 }, { 133, 104, 89 }, { 210, 125, 72 },
            { 73, 48, 37 }, { 50, 32, 25 }, { 87, 60, 47 }, { 46, 29, 23 }, { 60, 40, 31 },
            { { 242, 144, 83 }, { 242, 83, 98 }, { 187, 242, 83 }, { 83, 242, 141 }, { 242, 221, 83 }, { 235, 214, 198 } }
        },
    {
            "coffee", true,
            { 43, 33, 26 }, { 228, 214, 197 }, { 133, 116, 99 }, { 190, 145, 100 },
            { 77, 60, 44 }, { 54, 42, 32 }, { 91, 74, 55 }, { 49, 38, 29 }, { 63, 50, 39 },
            { { 242, 176, 109 }, { 242, 112, 109 }, { 181, 242, 109 }, { 109, 242, 173 }, { 242, 240, 109 }, { 228, 214, 197 } }
        },
    {
            "cyan", true,
            { 8, 28, 36 }, { 204, 235, 242 }, { 91, 124, 135 }, { 52, 211, 235 },
            { 25, 55, 66 }, { 14, 40, 50 }, { 43, 72, 84 }, { 12, 35, 45 }, { 20, 48, 59 },
            { { 54, 218, 242 }, { 54, 242, 176 }, { 71, 54, 242 }, { 242, 54, 221 }, { 54, 127, 242 }, { 204, 235, 242 } }
        },
    {
            "cyberpunk", true,
            { 10, 14, 18 }, { 230, 241, 240 }, { 102, 119, 118 }, { 0, 240, 255 },
            { 28, 45, 48 }, { 16, 23, 28 }, { 43, 65, 68 }, { 14, 21, 25 }, { 22, 33, 38 },
            { { 0, 228, 242 }, { 0, 242, 140 }, { 5, 0, 242 }, { 242, 0, 233 }, { 0, 112, 242 }, { 230, 241, 240 } }
        },
    {
            "dark", true,
            { 22, 22, 26 }, { 210, 205, 195 }, { 90, 85, 80 }, { 200, 175, 130 },
            { 60, 55, 45 }, { 28, 28, 32 }, { 50, 48, 45 }, { 30, 30, 34 }, { 35, 35, 40 },
            { { 242, 195, 109 }, { 242, 131, 109 }, { 162, 242, 109 }, { 109, 242, 192 }, { 226, 242, 109 }, { 210, 205, 195 } }
        },
    {
            "dracula", true,
            { 40, 42, 54 }, { 248, 248, 242 }, { 98, 114, 134 }, { 189, 147, 249 },
            { 68, 71, 90 }, { 48, 50, 65 }, { 68, 71, 90 }, { 48, 50, 65 }, { 50, 52, 68 },
            { { 164, 109, 242 }, { 109, 118, 242 }, { 242, 109, 193 }, { 242, 161, 109 }, { 228, 109, 242 }, { 248, 248, 242 } }
        },
    {
            "emerald", true,
            { 13, 31, 28 }, { 205, 235, 225 }, { 91, 126, 116 }, { 52, 211, 153 },
            { 31, 59, 52 }, { 18, 41, 36 }, { 47, 76, 66 }, { 16, 37, 33 }, { 24, 48, 42 },
            { { 60, 242, 176 }, { 60, 242, 88 }, { 60, 134, 242 }, { 172, 60, 242 }, { 60, 221, 242 }, { 205, 235, 225 } }
        },
    {
            "forest", true,
            { 18, 35, 27 }, { 211, 230, 216 }, { 99, 127, 108 }, { 86, 170, 110 },
            { 40, 65, 48 }, { 24, 46, 34 }, { 55, 79, 62 }, { 22, 42, 31 }, { 29, 52, 38 },
            { { 109, 242, 147 }, { 135, 242, 109 }, { 109, 210, 242 }, { 144, 109, 242 }, { 109, 242, 211 }, { 211, 230, 216 } }
        },
    {
            "fuchsia", true,
            { 43, 17, 38 }, { 239, 210, 231 }, { 132, 91, 121 }, { 236, 72, 153 },
            { 77, 39, 69 }, { 53, 23, 47 }, { 91, 48, 81 }, { 48, 20, 43 }, { 61, 29, 55 },
            { { 242, 74, 157 }, { 242, 74, 238 }, { 242, 152, 74 }, { 160, 242, 74 }, { 242, 74, 76 }, { 239, 210, 231 } }
        },
    {
            "gruvbox", true,
            { 40, 40, 40 }, { 235, 219, 178 }, { 146, 131, 116 }, { 215, 153, 33 },
            { 80, 73, 69 }, { 50, 48, 47 }, { 80, 73, 69 }, { 50, 48, 47 }, { 60, 56, 54 },
            { { 242, 172, 37 }, { 242, 74, 37 }, { 115, 242, 37 }, { 37, 242, 168 }, { 214, 242, 37 }, { 235, 219, 178 } }
        },
    {
            "halloween", true,
            { 25, 17, 11 }, { 245, 230, 205 }, { 135, 111, 88 }, { 255, 125, 0 },
            { 63, 44, 27 }, { 34, 24, 16 }, { 83, 56, 32 }, { 29, 21, 14 }, { 45, 31, 20 },
            { { 242, 119, 0 }, { 242, 2, 0 }, { 133, 242, 0 }, { 0, 242, 114 }, { 242, 235, 0 }, { 245, 230, 205 } }
        },
    {
            "high-contrast", false,
            { 0, 0, 0 }, { 255, 255, 255 }, { 170, 170, 170 }, { 255, 221, 0 },
            { 50, 50, 50 }, { 15, 15, 15 }, { 100, 100, 100 }, { 10, 10, 10 }, { 25, 25, 25 },
            { { 242, 210, 0 }, { 242, 94, 0 }, { 42, 242, 0 }, { 0, 242, 205 }, { 158, 242, 0 }, { 255, 255, 255 } }
        },
    {
            "lime", true,
            { 25, 32, 18 }, { 220, 235, 193 }, { 116, 130, 91 }, { 164, 214, 58 },
            { 51, 62, 34 }, { 34, 43, 24 }, { 69, 81, 47 }, { 30, 39, 22 }, { 39, 49, 28 },
            { { 186, 242, 66 }, { 242, 214, 66 }, { 66, 242, 115 }, { 66, 189, 242 }, { 101, 242, 66 }, { 220, 235, 193 } }
        },
    {
            "midnight", true,
            { 12, 16, 24 }, { 205, 214, 228 }, { 83, 96, 117 }, { 92, 140, 255 },
            { 31, 39, 53 }, { 18, 23, 34 }, { 43, 52, 68 }, { 17, 22, 32 }, { 24, 29, 42 },
            { { 87, 133, 242 }, { 87, 207, 242 }, { 190, 87, 242 }, { 242, 87, 136 }, { 116, 87, 242 }, { 205, 214, 228 } }
        },
    {
            "mint", true,
            { 17, 35, 33 }, { 208, 235, 226 }, { 102, 132, 124 }, { 80, 200, 170 },
            { 38, 62, 58 }, { 23, 46, 43 }, { 54, 80, 73 }, { 21, 42, 40 }, { 29, 53, 49 },
            { { 97, 242, 206 }, { 97, 242, 136 }, { 97, 139, 242 }, { 203, 97, 242 }, { 97, 209, 242 }, { 208, 235, 226 } }
        },
    {
            "monokai", true,
            { 39, 40, 34 }, { 248, 248, 242 }, { 117, 113, 94 }, { 166, 226, 46 },
            { 73, 72, 62 }, { 50, 50, 43 }, { 89, 88, 74 }, { 47, 48, 41 }, { 60, 61, 51 },
            { { 178, 242, 49 }, { 242, 214, 49 }, { 49, 242, 106 }, { 49, 182, 242 }, { 85, 242, 49 }, { 248, 248, 242 } }
        },
    {
            "nord", true,
            { 46, 52, 64 }, { 216, 222, 233 }, { 128, 139, 155 }, { 136, 192, 208 },
            { 67, 76, 94 }, { 52, 59, 72 }, { 76, 86, 106 }, { 59, 66, 82 }, { 55, 62, 76 },
            { { 109, 213, 242 }, { 109, 242, 208 }, { 133, 109, 242 }, { 242, 109, 215 }, { 109, 149, 242 }, { 216, 222, 233 } }
        },
    {
            "ocean", true,
            { 10, 30, 55 }, { 205, 222, 245 }, { 91, 113, 143 }, { 80, 160, 255 },
            { 30, 56, 89 }, { 16, 40, 68 }, { 47, 74, 108 }, { 14, 35, 61 }, { 23, 48, 78 },
            { { 76, 152, 242 }, { 76, 232, 242 }, { 160, 76, 242 }, { 242, 76, 155 }, { 80, 76, 242 }, { 205, 222, 245 } }
        },
    {
            "one-dark", true,
            { 40, 44, 52 }, { 171, 178, 191 }, { 92, 99, 112 }, { 97, 175, 239 },
            { 58, 63, 72 }, { 45, 49, 58 }, { 68, 74, 85 }, { 34, 38, 46 }, { 49, 54, 63 },
            { { 98, 177, 242 }, { 98, 242, 238 }, { 157, 98, 242 }, { 242, 98, 180 }, { 98, 108, 242 }, { 171, 178, 191 } }
        },
    {
            "orange", true,
            { 43, 25, 14 }, { 244, 221, 197 }, { 145, 111, 82 }, { 255, 139, 61 },
            { 81, 51, 29 }, { 55, 34, 20 }, { 94, 62, 37 }, { 49, 29, 17 }, { 63, 40, 24 },
            { { 242, 132, 58 }, { 242, 58, 72 }, { 176, 242, 58 }, { 58, 242, 128 }, { 242, 221, 58 }, { 244, 221, 197 } }
        },
    {
            "purple", true,
            { 30, 21, 43 }, { 225, 211, 241 }, { 118, 99, 134 }, { 180, 120, 255 },
            { 67, 49, 82 }, { 39, 28, 55 }, { 78, 58, 94 }, { 35, 25, 49 }, { 48, 36, 64 },
            { { 168, 109, 242 }, { 109, 114, 242 }, { 242, 109, 188 }, { 242, 166, 109 }, { 232, 109, 242 }, { 225, 211, 241 } }
        },
    {
            "red", true,
            { 42, 17, 17 }, { 245, 215, 213 }, { 139, 91, 89 }, { 239, 83, 80 },
            { 78, 38, 37 }, { 53, 23, 23 }, { 92, 48, 47 }, { 47, 20, 20 }, { 61, 28, 27 },
            { { 242, 84, 81 }, { 242, 81, 155 }, { 242, 239, 81 }, { 81, 242, 81 }, { 242, 161, 81 }, { 245, 215, 213 } }
        },
    {
            "rose", true,
            { 45, 20, 28 }, { 240, 214, 220 }, { 137, 95, 105 }, { 244, 114, 140 },
            { 78, 42, 51 }, { 55, 27, 35 }, { 91, 51, 60 }, { 50, 24, 32 }, { 63, 32, 41 },
            { { 242, 109, 136 }, { 242, 109, 200 }, { 242, 210, 109 }, { 138, 242, 109 }, { 242, 146, 109 }, { 240, 214, 220 } }
        },
    {
            "solarized-dark", true,
            { 0, 43, 54 }, { 131, 148, 150 }, { 88, 110, 117 }, { 38, 139, 210 },
            { 7, 54, 66 }, { 5, 50, 62 }, { 7, 54, 66 }, { 5, 50, 62 }, { 4, 52, 63 },
            { { 44, 160, 242 }, { 44, 242, 229 }, { 118, 44, 242 }, { 242, 44, 164 }, { 44, 65, 242 }, { 131, 148, 150 } }
        },
    {
            "synthwave", true,
            { 25, 15, 38 }, { 236, 224, 247 }, { 122, 99, 139 }, { 255, 55, 190 },
            { 63, 36, 78 }, { 35, 21, 52 }, { 83, 46, 100 }, { 29, 18, 44 }, { 45, 26, 61 },
            { { 242, 52, 181 }, { 213, 52, 242 }, { 242, 106, 52 }, { 184, 242, 52 }, { 242, 52, 89 }, { 236, 224, 247 } }
        },
    {
            "teal", true,
            { 10, 31, 34 }, { 202, 232, 234 }, { 92, 127, 132 }, { 45, 190, 184 },
            { 29, 59, 62 }, { 16, 42, 45 }, { 45, 74, 77 }, { 14, 38, 41 }, { 23, 50, 54 },
            { { 57, 242, 235 }, { 57, 242, 146 }, { 57, 72, 242 }, { 231, 57, 242 }, { 57, 161, 242 }, { 202, 232, 234 } }
        },
    {
            "terminal-green", true,
            { 5, 16, 8 }, { 191, 255, 198 }, { 77, 133, 87 }, { 57, 255, 94 },
            { 18, 48, 23 }, { 9, 25, 13 }, { 30, 70, 36 }, { 7, 21, 11 }, { 12, 33, 17 },
            { { 54, 242, 89 }, { 109, 242, 54 }, { 54, 215, 242 }, { 86, 54, 242 }, { 54, 242, 180 }, { 191, 255, 198 } }
        },
    {
            "tokyo-night", true,
            { 26, 27, 38 }, { 192, 202, 245 }, { 86, 95, 137 }, { 122, 162, 247 },
            { 45, 48, 65 }, { 32, 33, 48 }, { 52, 55, 75 }, { 30, 31, 44 }, { 38, 40, 57 },
            { { 109, 152, 242 }, { 109, 216, 242 }, { 194, 109, 242 }, { 242, 109, 154 }, { 130, 109, 242 }, { 192, 202, 245 } }
        },
    {
            "violet", true,
            { 28, 22, 42 }, { 226, 218, 245 }, { 119, 108, 137 }, { 157, 122, 255 },
            { 64, 51, 82 }, { 38, 31, 54 }, { 76, 61, 94 }, { 34, 28, 49 }, { 47, 39, 62 },
            { { 144, 109, 242 }, { 109, 138, 242 }, { 242, 109, 213 }, { 242, 141, 109 }, { 208, 109, 242 }, { 226, 218, 245 } }
        }
};

// #endregion

// #region Output Primitives

void set_fg(DawnColor c)
{
    DAWN_BACKEND(app)->set_fg(c);
}

void set_bg(DawnColor c)
{
    DAWN_BACKEND(app)->set_bg(c);
}

void move_to(int32_t r, int32_t c)
{
    DAWN_BACKEND(app)->set_cursor(c, r); // Note: backend uses (col, row) order
}

void out_str(const char* str)
{
    DAWN_BACKEND(app)->write_str(str, strlen(str));
}

void out_str_n(const char* str, size_t len)
{
    DAWN_BACKEND(app)->write_str(str, len);
}

void out_char(char c)
{
    DAWN_BACKEND(app)->write_char(c);
}

void out_spaces(int32_t n)
{
    DAWN_BACKEND(app)->repeat_char(' ', n);
}

void out_int(int32_t value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    out_str(buf);
}

void out_flush(void)
{
    DAWN_BACKEND(app)->flush();
}

void clear_screen(void)
{
    DAWN_BACKEND(app)->clear_screen();
}

void clear_line(void)
{
    DAWN_BACKEND(app)->clear_line();
}

void clear_range(int32_t n)
{
    DAWN_BACKEND(app)->clear_range(n);
}

void cursor_visible(bool visible)
{
    DAWN_BACKEND(app)->set_cursor_visible(visible);
}

void cursor_home(void)
{
    move_to(1, 1);
}

void sync_begin(void)
{
    DAWN_BACKEND(app)->sync_begin();
}

void sync_end(void)
{
    DAWN_BACKEND(app)->sync_end();
}

void fill_line_end(DawnColor bg)
{
    // Only applies in print mode
    if (app.ctx.mode != DAWN_MODE_PRINT)
        return;

    int32_t cols = 0, rows = 0;
    DAWN_BACKEND(app)->get_size(&cols, &rows);
    if (cols <= 0)
        cols = 80;

    // Set background and fill to end of line
    set_bg(bg);
    // Use CSI K (erase to end of line) which uses current bg
    DAWN_BACKEND(app)->write_str("\x1b[K", 3);
}

// #endregion

// #region Theme Colors

const char* theme_name(Theme theme)
{
    if ((int)theme < 0 || theme >= THEME_COUNT)
        return "light";
    return THEMES[theme].name;
}

bool theme_is_dark(Theme theme)
{
    if ((int)theme < 0 || theme >= THEME_COUNT)
        return false;
    return THEMES[theme].dark;
}

Theme theme_next(Theme theme)
{
    return (Theme)(((int)theme + 1) % THEME_COUNT);
}

Theme theme_from_name(const char* name)
{
    if (!name)
        return THEME_COUNT;

    for (int i = 0; i < THEME_COUNT; ++i) {
        if (strcmp(name, THEMES[i].name) == 0)
            return (Theme)i;
    }
    return THEME_COUNT;
}

static const ThemePalette* current_theme(void)
{
    if ((int)app.theme < 0 || app.theme >= THEME_COUNT)
        return &THEMES[THEME_LIGHT];
    return &THEMES[app.theme];
}

DawnColor get_bg(void)
{
    if (app.ctx.mode == DAWN_MODE_PRINT && app.ctx.host_bg)
        return *app.ctx.host_bg;
    return current_theme()->bg;
}

DawnColor get_fg(void) { return current_theme()->fg; }
DawnColor get_dim(void) { return current_theme()->dim; }
DawnColor get_accent(void) { return current_theme()->accent; }
DawnColor get_select(void) { return current_theme()->select; }
DawnColor get_ai_bg(void) { return current_theme()->ai_bg; }
DawnColor get_border(void) { return current_theme()->border; }
DawnColor get_code_bg(void) { return current_theme()->code_bg; }
DawnColor get_modal_bg(void) { return current_theme()->modal_bg; }

DawnColor get_heading(int32_t level)
{
    if (level < 1) level = 1;
    if (level > 6) level = 6;
    return current_theme()->headings[level - 1];
}

// #endregion

// #region DawnColor Utilities

DawnColor color_lerp(DawnColor a, DawnColor b, float t)
{
    return (DawnColor) {
        (uint8_t)(a.r + (b.r - a.r) * t),
        (uint8_t)(a.g + (b.g - a.g) * t),
        (uint8_t)(a.b + (b.b - a.b) * t)
    };
}

// #endregion

// #region Text Attributes

void set_bold(bool on)
{
    DAWN_BACKEND(app)->set_bold(on);
}

void set_italic(bool on)
{
    DAWN_BACKEND(app)->set_italic(on);
}

void set_dim(bool on)
{
    DAWN_BACKEND(app)->set_dim(on);
}

void set_strikethrough(bool on)
{
    DAWN_BACKEND(app)->set_strike(on);
}

void reset_attrs(void)
{
    DAWN_BACKEND(app)->reset_attrs();
}

// #endregion

// #region Styled Text

void set_underline(UnderlineStyle style)
{
    DAWN_BACKEND(app)->set_underline(style);
}

void set_underline_color(DawnColor c)
{
    DAWN_BACKEND(app)->set_underline_color(c);
}

void clear_underline(void)
{
    DAWN_BACKEND(app)->clear_underline();
}

// #endregion

// #region Text Sizing

void print_scaled_char(char c, int32_t scale)
{
    if (scale <= 1 || !dawn_ctx_has(&app.ctx, DAWN_CAP_TEXT_SIZING)) {
        DAWN_BACKEND(app)->write_char(c);
        return;
    }
    char str[2] = { c, '\0' };
    DAWN_BACKEND(app)->write_scaled(str, 1, scale);
}

void print_scaled_str(const char* str, size_t len, int32_t scale)
{
    if (scale <= 1 || !dawn_ctx_has(&app.ctx, DAWN_CAP_TEXT_SIZING)) {
        DAWN_BACKEND(app)->write_str(str, len);
        return;
    }
    DAWN_BACKEND(app)->write_scaled(str, len, scale);
}

void print_scaled_frac_char(char c, int32_t scale, int32_t num, int32_t denom)
{
    // No scaling needed if scale is 1 with no fractional part, or no text sizing support
    if ((scale <= 1 && (num == 0 || denom == 0)) || !dawn_ctx_has(&app.ctx, DAWN_CAP_TEXT_SIZING)) {
        DAWN_BACKEND(app)->write_char(c);
        return;
    }
    if (DAWN_BACKEND(app)->write_scaled_frac) {
        char str[2] = { c, '\0' };
        DAWN_BACKEND(app)->write_scaled_frac(str, 1, scale, num, denom);
    } else if (scale > 1) {
        // Fallback to integer scaling if fractional not supported
        char str[2] = { c, '\0' };
        DAWN_BACKEND(app)->write_scaled(str, 1, scale);
    } else {
        DAWN_BACKEND(app)->write_char(c);
    }
}

void print_scaled_frac_str(const char* str, size_t len, int32_t scale, int32_t num, int32_t denom)
{
    // No scaling needed if scale is 1 with no fractional part, or no text sizing support
    if ((scale <= 1 && (num == 0 || denom == 0)) || !dawn_ctx_has(&app.ctx, DAWN_CAP_TEXT_SIZING)) {
        DAWN_BACKEND(app)->write_str(str, len);
        return;
    }
    if (DAWN_BACKEND(app)->write_scaled_frac) {
        DAWN_BACKEND(app)->write_scaled_frac(str, len, scale, num, denom);
    } else if (scale > 1) {
        // Fallback to integer scaling if fractional not supported
        DAWN_BACKEND(app)->write_scaled(str, len, scale);
    } else {
        DAWN_BACKEND(app)->write_str(str, len);
    }
}

// #endregion
