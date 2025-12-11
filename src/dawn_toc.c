// dawn_toc.c

#include "dawn_toc.h"
#include "dawn_gap.h"
#include "dawn_md.h"
#include "dawn_utils.h"
#include <string.h>

// #region FZF-Style Fuzzy Matching

// Scoring constants (from fzf)
#define SCORE_MATCH           16
#define SCORE_GAP_START       -3
#define SCORE_GAP_EXTENSION   -1
#define BONUS_BOUNDARY        (SCORE_MATCH / 2)   // 8
#define BONUS_NON_WORD        (SCORE_MATCH / 2)   // 8
#define BONUS_CAMEL           (BONUS_BOUNDARY + SCORE_GAP_EXTENSION)  // 7
#define BONUS_CONSECUTIVE     (-(SCORE_GAP_START + SCORE_GAP_EXTENSION))  // 4
#define BONUS_FIRST_CHAR_MULT 2
#define BONUS_BOUNDARY_WHITE  (BONUS_BOUNDARY + 2)  // 10
#define BONUS_BOUNDARY_DELIM  (BONUS_BOUNDARY + 1)  // 9

//! Character class for bonus calculation
DAWN_ENUM(uint8_t) {
    CHAR_WHITE,
    CHAR_NON_WORD,
    CHAR_DELIMITER,
    CHAR_LOWER,
    CHAR_UPPER,
    CHAR_LETTER,
    CHAR_NUMBER
} CharClass;

//! Get character class
static CharClass char_class_of(char c) {
    if (ISLOWER_(c)) return CHAR_LOWER;
    if (ISUPPER_(c)) return CHAR_UPPER;
    if (ISDIGIT_(c)) return CHAR_NUMBER;
    if (ISSPACE_(c)) return CHAR_WHITE;
    if (c == '/' || c == ',' || c == ':' || c == ';' || c == '|') return CHAR_DELIMITER;
    if (c == '_' || c == '-') return CHAR_DELIMITER;
    return CHAR_NON_WORD;
}

//! Calculate bonus for transition between character classes
static int32_t bonus_for(CharClass prev_class, CharClass curr_class) {
    // Word character after boundary
    if (curr_class > CHAR_NON_WORD) {
        switch (prev_class) {
            case CHAR_WHITE:
                return BONUS_BOUNDARY_WHITE;
            case CHAR_DELIMITER:
                return BONUS_BOUNDARY_DELIM;
            case CHAR_NON_WORD:
                return BONUS_BOUNDARY;
            default:
                break;
        }
    }

    // camelCase or letter123 transition
    if ((prev_class == CHAR_LOWER && curr_class == CHAR_UPPER) ||
        (prev_class != CHAR_NUMBER && curr_class == CHAR_NUMBER)) {
        return BONUS_CAMEL;
    }

    // Non-word bonus
    if (curr_class == CHAR_NON_WORD || curr_class == CHAR_DELIMITER) {
        return BONUS_NON_WORD;
    }
    if (curr_class == CHAR_WHITE) {
        return BONUS_BOUNDARY_WHITE;
    }

    return 0;
}

//! FZF V1-style fuzzy match with scoring
//! Returns score (0 = no match, higher = better)
static int32_t fuzzy_match(const char *pattern, int32_t plen, const char *text, int32_t tlen) {
    if (plen == 0) return 1;  // Empty pattern matches everything

    // Forward pass: find first occurrence of pattern
    int32_t pidx = 0;
    int32_t sidx = -1;  // Start index
    int32_t eidx = -1;  // End index

    for (int32_t idx = 0; idx < tlen && pidx < plen; idx++) {
        char pc = TOLOWER_(pattern[pidx]);
        char tc = TOLOWER_(text[idx]);

        if (pc == tc) {
            if (sidx < 0) sidx = idx;
            eidx = idx;
            pidx++;
        }
    }

    // No match if we didn't find all pattern chars
    if (pidx != plen) return 0;

    // Backward pass: find shorter match
    pidx = plen - 1;
    int32_t best_end = eidx;

    for (int32_t idx = eidx; idx >= sidx && pidx >= 0; idx--) {
        char pc = TOLOWER_(pattern[pidx]);
        char tc = TOLOWER_(text[idx]);

        if (pc == tc) {
            if (pidx == plen - 1) best_end = idx;
            pidx--;
        }
    }

    // Calculate score for the match region [sidx, best_end]
    int32_t score = 0;
    int32_t consecutive = 0;
    int32_t first_bonus = 0;
    CharClass prev_class = (sidx > 0) ? char_class_of(text[sidx - 1]) : CHAR_WHITE;
    bool in_gap = false;

    pidx = 0;
    for (int32_t idx = sidx; idx <= best_end && pidx < plen; idx++) {
        char tc = text[idx];
        CharClass curr_class = char_class_of(tc);

        char pc = TOLOWER_(pattern[pidx]);
        char tc_lower = TOLOWER_(tc);

        if (pc == tc_lower) {
            // Match
            score += SCORE_MATCH;

            int32_t bonus = bonus_for(prev_class, curr_class);

            if (consecutive == 0) {
                first_bonus = bonus;
            } else {
                // Break consecutive chunk if we have a better boundary
                if (bonus >= BONUS_BOUNDARY && bonus > first_bonus) {
                    first_bonus = bonus;
                }
                // Use max of current bonus, first_bonus, or consecutive bonus
                if (first_bonus > bonus) bonus = first_bonus;
                if (BONUS_CONSECUTIVE > bonus) bonus = BONUS_CONSECUTIVE;
            }

            if (pidx == 0) {
                score += bonus * BONUS_FIRST_CHAR_MULT;
            } else {
                score += bonus;
            }

            in_gap = false;
            consecutive++;
            pidx++;
        } else {
            // Gap
            if (in_gap) {
                score += SCORE_GAP_EXTENSION;
            } else {
                score += SCORE_GAP_START;
            }
            in_gap = true;
            consecutive = 0;
            first_bonus = 0;
        }

        prev_class = curr_class;
    }

    return score > 0 ? score : 1;  // Ensure positive score for matches
}

// #endregion

// #region TOC Building

void toc_init(TocState *state) {
    memset(state, 0, sizeof(TocState));
    state->filtered = malloc(TOC_MAX_ENTRIES * sizeof(int32_t));
}

void toc_free(TocState *state) {
    if (state->filtered) {
        free(state->filtered);
        state->filtered = NULL;
    }
}

void toc_build(const GapBuffer *gb, TocState *state) {
    state->count = 0;
    size_t len = gap_len(gb);
    size_t pos = 0;

    // Track whether we're inside a code block or block math
    bool in_code_block = false;
    bool in_block_math = false;

    while (pos < len && state->count < TOC_MAX_ENTRIES) {
        MdSpan lang;
        if (md_check_code_fence(gb, pos, &lang)) {
            in_code_block = !in_code_block;
            while (pos < len && gap_at(gb, pos) != '\n') pos++;
            if (pos < len) pos++;
            continue;
        }

        MdMatch math;
        if (md_check_block_math(gb, pos, &math)) {
            in_block_math = !in_block_math;
            while (pos < len && gap_at(gb, pos) != '\n') pos++;
            if (pos < len) pos++;
            continue;
        }

        if (!in_code_block && !in_block_math) {
            MdStyle header = md_check_header(gb, pos);

            if (header) {
                TocEntry *entry = &state->entries[state->count];
                entry->pos = pos;

                if (header & MD_H1) entry->level = 1;
                else if (header & MD_H2) entry->level = 2;
                else if (header & MD_H3) entry->level = 3;
                else if (header & MD_H4) entry->level = 4;
                else if (header & MD_H5) entry->level = 5;
                else if (header & MD_H6) entry->level = 6;
                else entry->level = 1;

                size_t content_start;
                md_check_header_content(gb, pos, &content_start);

                int32_t ti = 0;
                size_t p = content_start;
                while (p < len && ti < TOC_MAX_HEADER_LEN - 1) {
                    char c = gap_at(gb, p);
                    if (c == '\n') break;
                    if (c == '{' && p + 1 < len && gap_at(gb, p + 1) == '#') {
                        break;
                    }
                    entry->text[ti++] = c;
                    p++;
                }

                while (ti > 0 && (entry->text[ti - 1] == ' ' || entry->text[ti - 1] == '\t')) {
                    ti--;
                }

                entry->text[ti] = '\0';
                entry->text_len = ti;

                if (ti > 0) {
                    state->count++;
                }
            }
        }

        while (pos < len && gap_at(gb, pos) != '\n') pos++;
        if (pos < len) pos++;
    }

    // Compute hierarchy depth for each entry using a stack of levels.
    // H1 followed by H2 means H2 is nested under H1.
    // H2 followed by H1 means H1 "pops" back to top level.
    if (state->count > 0) {
        int32_t level_stack[7] = {0};
        int32_t stack_depth = 0;

        for (int32_t i = 0; i < state->count; i++) {
            TocEntry *entry = &state->entries[i];
            int32_t level = entry->level;

            while (stack_depth > 0 && level_stack[stack_depth - 1] >= level) {
                stack_depth--;
            }

            entry->depth = stack_depth;

            if (stack_depth < 6) {
                level_stack[stack_depth] = level;
                stack_depth++;
            }
        }
    }

    toc_filter(state);
}

void toc_filter(TocState *state) {
    state->filtered_count = 0;

    if (state->filter_len == 0) {
        for (int32_t i = 0; i < state->count; i++) {
            state->filtered[state->filtered_count++] = i;
        }
    } else {
        typedef struct { int32_t idx; int32_t score; } scored_t;
        scored_t scored[TOC_MAX_ENTRIES];
        int32_t scored_count = 0;

        for (int32_t i = 0; i < state->count; i++) {
            TocEntry *e = &state->entries[i];
            int32_t score = fuzzy_match(state->filter, state->filter_len, e->text, e->text_len);
            if (score > 0) {
                scored[scored_count].idx = i;
                scored[scored_count].score = score;
                scored_count++;
            }
        }

        for (int32_t i = 1; i < scored_count; i++) {
            scored_t tmp = scored[i];
            int32_t j = i - 1;
            while (j >= 0 && scored[j].score < tmp.score) {
                scored[j + 1] = scored[j];
                j--;
            }
            scored[j + 1] = tmp;
        }

        for (int32_t i = 0; i < scored_count; i++) {
            state->filtered[state->filtered_count++] = scored[i].idx;
        }
    }

    if (state->selected >= state->filtered_count) {
        state->selected = state->filtered_count > 0 ? state->filtered_count - 1 : 0;
    }
    state->scroll = 0;
}

const TocEntry *toc_get_selected(const TocState *state) {
    if (state->filtered_count == 0) return NULL;
    int32_t idx = state->filtered[state->selected];
    return &state->entries[idx];
}

// #endregion
