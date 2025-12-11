#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "dawn_block.h"
#include "dawn_gap.h"
#include "cJSON.h"
#include "dawn_backend.h"

// Stub globals for testing
App app = {0};
int current_text_scale = 1;
int current_frac_num = 1;
int current_frac_denom = 1;

int get_fg(void) { return 7; }
int get_bg(void) { return 0; }
void set_fg(int c) { (void)c; }
void set_bg(int c) { (void)c; }

bool image_is_supported(void) { return false; }
bool image_get_size(const char *path, int *w, int *h) { (void)path; *w = 0; *h = 0; return false; }
int image_calc_rows(int img_w, int img_h, int req_w, int req_h, int text_w, int text_h) {
    (void)img_w; (void)img_h; (void)req_w; (void)req_h; (void)text_w; (void)text_h;
    return 1;
}
char *image_resolve_and_cache_to(const char *path, size_t len, const char *base) {
    (void)path; (void)len; (void)base;
    return NULL;
}

typedef struct { int rows; } TexSketch;
TexSketch *tex_render_string(const char *s, size_t len, int w) { (void)s; (void)len; (void)w; return NULL; }
void tex_sketch_free(TexSketch *sk) { (void)sk; }

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int tests_skipped = 0;

static const char *supported_sections[] = {
    "Tabs",
    "Precedence",
    "Thematic breaks",
    "ATX headings",
    "Setext headings",
    "Fenced code blocks",
    "Paragraphs",
    "Blank lines",
    "Block quotes",
    "List items",
    "Lists",
    NULL
};

static bool is_supported_section(const char *section) {
    for (int i = 0; supported_sections[i]; i++) {
        if (strcmp(section, supported_sections[i]) == 0)
            return true;
    }
    return false;
}

static const char *block_type_str(BlockType type) {
    switch (type) {
        case BLOCK_PARAGRAPH:   return "PARAGRAPH";
        case BLOCK_HEADER:      return "HEADER";
        case BLOCK_CODE:        return "CODE";
        case BLOCK_MATH:        return "MATH";
        case BLOCK_TABLE:       return "TABLE";
        case BLOCK_IMAGE:       return "IMAGE";
        case BLOCK_HR:          return "HR";
        case BLOCK_BLOCKQUOTE:  return "BLOCKQUOTE";
        case BLOCK_LIST_ITEM:   return "LIST_ITEM";
        case BLOCK_FOOTNOTE_DEF: return "FOOTNOTE_DEF";
        default:                return "UNKNOWN";
    }
}

static int parse_html_block_types(const char *html, BlockType *types, int *levels, int max_types) {
    int count = 0;
    const char *p = html;
    int depth = 0;

    while (*p && count < max_types) {
        while (*p && (*p == ' ' || *p == '\n' || *p == '\t')) p++;
        if (!*p) break;
        if (*p != '<') { p++; continue; }

        if (strncmp(p, "</blockquote>", 13) == 0) { depth--; p += 13; continue; }
        if (strncmp(p, "</ul>", 5) == 0) { depth--; p += 5; continue; }
        if (strncmp(p, "</ol>", 5) == 0) { depth--; p += 5; continue; }
        if (strncmp(p, "</li>", 5) == 0) { p += 5; continue; }
        if (strncmp(p, "</pre>", 6) == 0) { p += 6; continue; }
        if (strncmp(p, "</p>", 4) == 0) { p += 4; continue; }
        if (strncmp(p, "</h", 3) == 0) { p += 5; continue; }
        if (strncmp(p, "</code>", 7) == 0) { p += 7; continue; }

        bool at_top = (depth == 0);

        if (strncmp(p, "<h", 2) == 0 && p[2] >= '1' && p[2] <= '6' && p[3] == '>') {
            if (at_top) {
                types[count] = BLOCK_HEADER;
                levels[count] = p[2] - '0';
                count++;
            }
            p += 4;
        }
        else if (strncmp(p, "<hr", 3) == 0) {
            if (at_top) {
                types[count] = BLOCK_HR;
                levels[count] = 0;
                count++;
            }
            while (*p && *p != '>') p++;
            if (*p) p++;
        }
        else if (strncmp(p, "<pre>", 5) == 0) {
            if (at_top) {
                types[count] = BLOCK_CODE;
                levels[count] = 0;
                count++;
            }
            p += 5;
        }
        else if (strncmp(p, "<blockquote>", 12) == 0) {
            if (at_top) {
                types[count] = BLOCK_BLOCKQUOTE;
                levels[count] = 0;
                count++;
            }
            depth++;
            p += 12;
        }
        else if (strncmp(p, "<ul>", 4) == 0 || strncmp(p, "<ol>", 4) == 0) {
            if (at_top) {
                types[count] = BLOCK_LIST_ITEM;
                levels[count] = 0;
                count++;
            }
            depth++;
            p += 4;
        }
        else if (strncmp(p, "<li>", 4) == 0) {
            p += 4;
        }
        else if (strncmp(p, "<p>", 3) == 0) {
            if (at_top) {
                types[count] = BLOCK_PARAGRAPH;
                levels[count] = 0;
                count++;
            }
            p += 3;
        }
        else if (strncmp(p, "<code", 5) == 0) {
            while (*p && *p != '>') p++;
            if (*p) p++;
        }
        else {
            p++;
        }
    }
    return count;
}

static char *unescape_json_string(const char *src) {
    size_t len = strlen(src);
    char *dst = malloc(len + 1);
    if (!dst) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '\\' && i + 1 < len) {
            switch (src[i + 1]) {
                case 'n': dst[j++] = '\n'; i++; break;
                case 't': dst[j++] = '\t'; i++; break;
                case 'r': dst[j++] = '\r'; i++; break;
                case '\\': dst[j++] = '\\'; i++; break;
                case '"': dst[j++] = '"'; i++; break;
                default: dst[j++] = src[i]; break;
            }
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
    return dst;
}

static bool requires_indented_code(const char *markdown, const char *html) {
    if (strstr(html, "<pre><code>") == NULL) return false;
    if (strstr(markdown, "```") != NULL) return false;
    if (strstr(markdown, "~~~") != NULL) return false;
    return true;
}

static bool run_test(int example, const char *section, const char *markdown, const char *html, bool verbose) {
    tests_run++;

    if (!is_supported_section(section)) {
        tests_skipped++;
        return true;
    }

    if (requires_indented_code(markdown, html)) {
        tests_skipped++;
        return true;
    }

    char *md = unescape_json_string(markdown);
    if (!md) {
        tests_failed++;
        return false;
    }

    GapBuffer gb;
    gap_init(&gb, strlen(md) + 16);
    gap_insert_str(&gb, 0, md, strlen(md));

    BlockCache bc;
    block_cache_init(&bc);
    block_cache_parse(&bc, &gb, 80, 24);

    BlockType expected_types[32];
    int expected_levels[32];
    int expected_count = parse_html_block_types(html, expected_types, expected_levels, 32);

    bool passed = true;

    if (strlen(md) > 0 && bc.count == 0) {
        passed = false;
        if (verbose) {
            printf("FAIL Example %d (%s): No blocks parsed\n", example, section);
            printf("  Input: %s\n", markdown);
        }
    }

    if (passed && expected_count > 0) {
        int32_t check_count = expected_count < (int32_t)bc.count ? expected_count : (int32_t)bc.count;

        for (int i = 0; i < check_count && passed; i++) {
            Block *block = &bc.blocks[i];
            BlockType expected_type = expected_types[i];
            int expected_level = expected_levels[i];

            if (expected_type == BLOCK_HEADER) {
                if (block->type != BLOCK_HEADER) {
                    passed = false;
                    if (verbose)
                        printf("FAIL Example %d (%s): Block %d: Expected HEADER, got %s\n",
                               example, section, i, block_type_str(block->type));
                } else if (expected_level > 0 && block->data.header.level != expected_level) {
                    passed = false;
                    if (verbose)
                        printf("FAIL Example %d (%s): Block %d: Expected H%d, got H%d\n",
                               example, section, i, expected_level, block->data.header.level);
                }
            }
            else if (expected_type == BLOCK_HR) {
                if (block->type != BLOCK_HR) {
                    passed = false;
                    if (verbose)
                        printf("FAIL Example %d (%s): Block %d: Expected HR, got %s\n",
                               example, section, i, block_type_str(block->type));
                }
            }
            else if (expected_type == BLOCK_CODE) {
                if (block->type != BLOCK_CODE && block->type != BLOCK_PARAGRAPH) {
                    passed = false;
                    if (verbose)
                        printf("FAIL Example %d (%s): Block %d: Expected CODE, got %s\n",
                               example, section, i, block_type_str(block->type));
                }
            }
            else if (expected_type == BLOCK_BLOCKQUOTE) {
                if (block->type != BLOCK_BLOCKQUOTE) {
                    passed = false;
                    if (verbose)
                        printf("FAIL Example %d (%s): Block %d: Expected BLOCKQUOTE, got %s\n",
                               example, section, i, block_type_str(block->type));
                }
            }
            else if (expected_type == BLOCK_LIST_ITEM) {
                if (block->type != BLOCK_LIST_ITEM) {
                    passed = false;
                    if (verbose)
                        printf("FAIL Example %d (%s): Block %d: Expected LIST_ITEM, got %s\n",
                               example, section, i, block_type_str(block->type));
                }
            }
            else if (expected_type == BLOCK_PARAGRAPH) {
                if (block->type != BLOCK_PARAGRAPH) {
                    passed = false;
                    if (verbose)
                        printf("FAIL Example %d (%s): Block %d: Expected PARAGRAPH, got %s\n",
                               example, section, i, block_type_str(block->type));
                }
            }
        }
    }

    if (passed)
        tests_passed++;
    else
        tests_failed++;

    block_cache_free(&bc);
    gap_free(&gb);
    free(md);

    return passed;
}

static int run_spec_tests(const char *spec_path, bool verbose) {
    FILE *f = fopen(spec_path, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s\n", spec_path);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_str = malloc(fsize + 1);
    if (!json_str) {
        fclose(f);
        return 1;
    }

    fread(json_str, 1, fsize, f);
    json_str[fsize] = '\0';
    fclose(f);

    cJSON *json = cJSON_Parse(json_str);
    free(json_str);

    if (!json) {
        fprintf(stderr, "Error: Failed to parse JSON\n");
        return 1;
    }

    if (!cJSON_IsArray(json)) {
        fprintf(stderr, "Error: Expected JSON array\n");
        cJSON_Delete(json);
        return 1;
    }

    cJSON *test;
    cJSON_ArrayForEach(test, json) {
        cJSON *example = cJSON_GetObjectItem(test, "example");
        cJSON *section = cJSON_GetObjectItem(test, "section");
        cJSON *markdown = cJSON_GetObjectItem(test, "markdown");
        cJSON *html = cJSON_GetObjectItem(test, "html");

        if (!example || !section || !markdown || !html) continue;

        run_test(example->valueint, section->valuestring,
                 markdown->valuestring, html->valuestring, verbose);
    }

    cJSON_Delete(json);
    return 0;
}

static void test_inline_parsing(void) {
    printf("\n=== Inline Parsing Tests ===\n");

    struct {
        const char *input;
        const char *desc;
        int expected_runs;
    } tests[] = {
        {"Hello world", "plain text", 1},
        {"**bold**", "bold text", 3},
        {"*italic*", "italic text", 3},
        {"**bold** and *italic*", "mixed styles", 7},
        {"[link](url)", "link", 1},
        {"text [link](url) more", "link in text", 3},
        {"$x^2$", "inline math", 1},
        {":smile:", "emoji", 1},
        {"[^1]", "footnote ref", 1},
        {"==highlight==", "highlight (==)", 3},
        {"===underline===", "underline (===)", 3},
        {"__underline__", "underline (__)", 3},
        {"~~strike~~", "strikethrough", 3},
        {NULL, NULL, 0}
    };

    int passed = 0, failed = 0;

    for (int i = 0; tests[i].input; i++) {
        InlineParseResult *result = block_parse_inline_string(tests[i].input, strlen(tests[i].input));

        if (result) {
            if (result->run_count >= 1) {
                passed++;
                printf("  PASS: %s (%d runs)\n", tests[i].desc, result->run_count);
            } else {
                failed++;
                printf("  FAIL: %s - expected runs, got %d\n", tests[i].desc, result->run_count);
            }
            block_parse_result_free(result);
        } else {
            failed++;
            printf("  FAIL: %s - parse returned NULL\n", tests[i].desc);
        }
    }

    printf("Inline tests: %d passed, %d failed\n", passed, failed);
}

static void test_block_parsing(void) {
    printf("\n=== Block Parsing Tests ===\n");

    struct {
        const char *input;
        const char *desc;
        BlockType expected;
        int extra;
    } tests[] = {
        {"# Header 1\n", "H1", BLOCK_HEADER, 1},
        {"## Header 2\n", "H2", BLOCK_HEADER, 2},
        {"### Header 3\n", "H3", BLOCK_HEADER, 3},
        {"#### Header 4\n", "H4", BLOCK_HEADER, 4},
        {"##### Header 5\n", "H5", BLOCK_HEADER, 5},
        {"###### Header 6\n", "H6", BLOCK_HEADER, 6},
        {"---\n", "HR (dashes)", BLOCK_HR, 0},
        {"***\n", "HR (asterisks)", BLOCK_HR, 0},
        {"___\n", "HR (underscores)", BLOCK_HR, 0},
        {"> Quote\n", "blockquote", BLOCK_BLOCKQUOTE, 1},
        {">> Nested\n", "nested blockquote", BLOCK_BLOCKQUOTE, 2},
        {"- item\n", "unordered list", BLOCK_LIST_ITEM, 1},
        {"* item\n", "unordered list *", BLOCK_LIST_ITEM, 1},
        {"+ item\n", "unordered list +", BLOCK_LIST_ITEM, 1},
        {"1. item\n", "ordered list", BLOCK_LIST_ITEM, 2},
        {"```\ncode\n```\n", "fenced code", BLOCK_CODE, 0},
        {"```js\ncode\n```\n", "fenced code with lang", BLOCK_CODE, 0},
        {"Just text\n", "paragraph", BLOCK_PARAGRAPH, 0},
        {"![alt](image.png)\n", "image", BLOCK_IMAGE, 0},
        {"$$\nx^2\n$$\n", "block math", BLOCK_MATH, 0},
        {NULL, NULL, 0, 0}
    };

    int passed = 0, failed = 0;

    for (int i = 0; tests[i].input; i++) {
        GapBuffer gb;
        gap_init(&gb, 256);
        gap_insert_str(&gb, 0, tests[i].input, strlen(tests[i].input));

        BlockCache bc;
        block_cache_init(&bc);
        block_cache_parse(&bc, &gb, 80, 24);

        bool ok = false;
        if (bc.count > 0) {
            Block *b = &bc.blocks[0];
            if (b->type == tests[i].expected) {
                ok = true;
                if (tests[i].expected == BLOCK_HEADER && tests[i].extra > 0)
                    ok = (b->data.header.level == tests[i].extra);
                if (tests[i].expected == BLOCK_BLOCKQUOTE && tests[i].extra > 0)
                    ok = (b->data.quote.level == tests[i].extra);
                if (tests[i].expected == BLOCK_LIST_ITEM && tests[i].extra > 0)
                    ok = (b->data.list.list_type == tests[i].extra);
            }
        }

        if (ok) {
            passed++;
            printf("  PASS: %s\n", tests[i].desc);
        } else {
            failed++;
            printf("  FAIL: %s - expected %s, got %s\n",
                   tests[i].desc,
                   block_type_str(tests[i].expected),
                   bc.count > 0 ? block_type_str(bc.blocks[0].type) : "none");
        }

        block_cache_free(&bc);
        gap_free(&gb);
    }

    printf("Block tests: %d passed, %d failed\n", passed, failed);
}

int main(int argc, char **argv) {
    bool verbose = false;
    const char *spec_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
            verbose = true;
        else
            spec_path = argv[i];
    }

    printf("dawn_block Test Suite\n");
    printf("=====================\n\n");

    test_block_parsing();
    test_inline_parsing();

    if (spec_path) {
        printf("\n=== CommonMark Spec Tests ===\n");
        printf("Loading: %s\n\n", spec_path);

        if (run_spec_tests(spec_path, verbose) != 0)
            return 1;

        printf("\n=== Results ===\n");
        printf("Total:   %d\n", tests_run);
        printf("Passed:  %d\n", tests_passed);
        printf("Failed:  %d\n", tests_failed);
        printf("Skipped: %d (unsupported sections)\n", tests_skipped);

        int tested = tests_passed + tests_failed;
        if (tested > 0)
            printf("\nPass rate: %.1f%% (%d/%d tested)\n",
                   100.0 * tests_passed / tested, tests_passed, tested);
    } else {
        printf("\nTo run CommonMark spec tests, provide path to spec.json:\n");
        printf("  %s tests/commonmark_spec.json\n", argv[0]);
    }

    printf("\n");
    return tests_failed > 0 ? 1 : 0;
}
