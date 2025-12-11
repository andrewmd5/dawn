// dawn_footnote.c

#include "dawn_footnote.h"
#include "dawn_md.h"
#include <string.h>
#include <stdlib.h>

// #region Types

//! Footnote tracking for navigation
typedef struct {
    char id[64];       //!< Footnote identifier
    size_t ref_pos;    //!< Position of first reference
    size_t def_pos;    //!< Position of definition, or SIZE_MAX if none
} FootnoteInfo;

// #endregion

// #region Internal Helpers

//! Scan document for all footnote references and definitions
//! @param gb gap buffer to scan
//! @param count output: number of footnotes found
//! @return array of FootnoteInfo (caller must free)
static FootnoteInfo *scan_footnotes(GapBuffer *gb, int32_t *count) {
    *count = 0;
    size_t len = gap_len(gb);
    FootnoteInfo *notes = NULL;
    int32_t capacity = 0;

    // First pass: find all references
    for (size_t pos = 0; pos < len; pos++) {
        MdMatch ref;
        if (md_check_footnote_ref(gb, pos, &ref)) {
            // Extract ID
            char id[64];
            size_t idl = ref.span.len < sizeof(id) - 1 ? ref.span.len : sizeof(id) - 1;
            for (size_t i = 0; i < idl; i++) {
                id[i] = gap_at(gb, ref.span.start + i);
            }
            id[idl] = '\0';

            // Check if we already have this ID
            bool found = false;
            for (int32_t i = 0; i < *count; i++) {
                if (strcmp(notes[i].id, id) == 0) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                if (*count >= capacity) {
                    capacity = capacity == 0 ? 8 : capacity * 2;
                    notes = realloc(notes, sizeof(FootnoteInfo) * (size_t)capacity);
                }
                strncpy(notes[*count].id, id, sizeof(notes[*count].id) - 1);
                notes[*count].id[sizeof(notes[*count].id) - 1] = '\0';
                notes[*count].ref_pos = pos;
                notes[*count].def_pos = SIZE_MAX;
                (*count)++;
            }

            pos += ref.total_len - 1;
        }
    }

    // Second pass: find definitions
    for (size_t pos = 0; pos < len; pos++) {
        MdMatch2 def;
        if (md_check_footnote_def(gb, pos, &def)) {
            char id[64];
            size_t idl = def.spans[0].len < sizeof(id) - 1 ? def.spans[0].len : sizeof(id) - 1;
            for (size_t i = 0; i < idl; i++) {
                id[i] = gap_at(gb, def.spans[0].start + i);
            }
            id[idl] = '\0';

            // Match to references
            for (int32_t i = 0; i < *count; i++) {
                if (strcmp(notes[i].id, id) == 0) {
                    notes[i].def_pos = pos;
                    break;
                }
            }
        }
    }

    return notes;
}

//! Create missing footnote definitions at end of document
//! @param gb gap buffer to modify
//! @return position of first new definition, or SIZE_MAX if none created
static size_t create_missing_footnotes(GapBuffer *gb) {
    int32_t count;
    FootnoteInfo *notes = scan_footnotes(gb, &count);
    if (!notes || count == 0) {
        free(notes);
        return SIZE_MAX;
    }

    // Find which ones are missing
    int32_t missing = 0;
    for (int32_t i = 0; i < count; i++) {
        if (notes[i].def_pos == SIZE_MAX) missing++;
    }

    if (missing == 0) {
        free(notes);
        return SIZE_MAX;
    }

    size_t len = gap_len(gb);
    size_t insert_pos = len;
    size_t first_new = SIZE_MAX;

    // Make sure there's a blank line before footnotes
    if (len > 0 && gap_at(gb, len - 1) != '\n') {
        gap_insert(gb, insert_pos, '\n');
        insert_pos++;
    }
    if (len > 1 && gap_at(gb, len - 2) != '\n') {
        gap_insert(gb, insert_pos, '\n');
        insert_pos++;
    }

    // Add separator
    const char *sep = "---\n\n";
    for (const char *p = sep; *p; p++) {
        gap_insert(gb, insert_pos, *p);
        insert_pos++;
    }

    // Add missing definitions
    for (int32_t i = 0; i < count; i++) {
        if (notes[i].def_pos == SIZE_MAX) {
            if (first_new == SIZE_MAX) {
                first_new = insert_pos;
            }

            // [^id]:
            gap_insert(gb, insert_pos, '[');
            insert_pos++;
            gap_insert(gb, insert_pos, '^');
            insert_pos++;
            for (const char *p = notes[i].id; *p; p++) {
                gap_insert(gb, insert_pos, *p);
                insert_pos++;
            }
            gap_insert(gb, insert_pos, ']');
            insert_pos++;
            gap_insert(gb, insert_pos, ':');
            insert_pos++;
            gap_insert(gb, insert_pos, ' ');
            insert_pos++;
            gap_insert(gb, insert_pos, '\n');
            insert_pos++;
            gap_insert(gb, insert_pos, '\n');
            insert_pos++;
        }
    }

    free(notes);
    return first_new;
}

// #endregion

// #region Footnote Navigation

void footnote_jump(GapBuffer *gb, size_t *cursor) {
    size_t len = gap_len(gb);
    size_t cur = *cursor;

    // Check if cursor is in a footnote reference
    MdMatch ref;
    if (md_check_footnote_ref(gb, cur, &ref)) {
        // Extract ID
        char id[64];
        size_t idl = ref.span.len < sizeof(id) - 1 ? ref.span.len : sizeof(id) - 1;
        for (size_t i = 0; i < idl; i++) {
            id[i] = gap_at(gb, ref.span.start + i);
        }
        id[idl] = '\0';

        // Find definition
        for (size_t pos = 0; pos < len; pos++) {
            MdMatch2 def;
            if (md_check_footnote_def(gb, pos, &def)) {
                char def_id[64];
                size_t dl = def.spans[0].len < sizeof(def_id) - 1 ? def.spans[0].len : sizeof(def_id) - 1;
                for (size_t i = 0; i < dl; i++) {
                    def_id[i] = gap_at(gb, def.spans[0].start + i);
                }
                def_id[dl] = '\0';

                if (strcmp(id, def_id) == 0) {
                    *cursor = def.spans[1].start;
                    return;
                }
            }
        }

        // No definition found - create it
        size_t new_pos = create_missing_footnotes(gb);
        if (new_pos != SIZE_MAX) {
            // Find our specific definition
            for (size_t pos = new_pos; pos < gap_len(gb); pos++) {
                MdMatch2 def;
                if (md_check_footnote_def(gb, pos, &def)) {
                    char def_id[64];
                    size_t dl = def.spans[0].len < sizeof(def_id) - 1 ? def.spans[0].len : sizeof(def_id) - 1;
                    for (size_t i = 0; i < dl; i++) {
                        def_id[i] = gap_at(gb, def.spans[0].start + i);
                    }
                    def_id[dl] = '\0';

                    if (strcmp(id, def_id) == 0) {
                        *cursor = def.spans[1].start;
                        return;
                    }
                }
            }
        }
        return;
    }

    // Check if we're somewhere that could be start of a reference (scan back to find it)
    for (size_t back = 0; back < 10 && cur >= back; back++) {
        size_t check_pos = cur - back;
        MdMatch ref_back;
        if (md_check_footnote_ref(gb, check_pos, &ref_back)) {
            if (check_pos + ref_back.total_len > cur) {
                // We're inside this reference - recurse with cursor at start
                size_t saved = cur;
                *cursor = check_pos;
                footnote_jump(gb, cursor);
                if (*cursor == check_pos) {
                    *cursor = saved;  // Restore if jump failed
                }
                return;
            }
        }
    }

    // Check if cursor is in a footnote definition
    size_t line_start = cur;
    while (line_start > 0 && gap_at(gb, line_start - 1) != '\n') {
        line_start--;
    }

    MdMatch2 def;
    if (md_check_footnote_def(gb, line_start, &def)) {
        // Extract ID
        char id[64];
        size_t idl = def.spans[0].len < sizeof(id) - 1 ? def.spans[0].len : sizeof(id) - 1;
        for (size_t i = 0; i < idl; i++) {
            id[i] = gap_at(gb, def.spans[0].start + i);
        }
        id[idl] = '\0';

        // Find first reference
        for (size_t pos = 0; pos < len; pos++) {
            MdMatch ref_find;
            if (md_check_footnote_ref(gb, pos, &ref_find)) {
                char ref_id[64];
                size_t rl = ref_find.span.len < sizeof(ref_id) - 1 ? ref_find.span.len : sizeof(ref_id) - 1;
                for (size_t i = 0; i < rl; i++) {
                    ref_id[i] = gap_at(gb, ref_find.span.start + i);
                }
                ref_id[rl] = '\0';

                if (strcmp(id, ref_id) == 0) {
                    *cursor = pos;
                    return;
                }
            }
        }
    }
}

bool footnote_create_definition(GapBuffer *gb, const char *id) {
    size_t len = gap_len(gb);

    // Check if definition already exists
    for (size_t pos = 0; pos < len; pos++) {
        MdMatch2 def;
        if (md_check_footnote_def(gb, pos, &def)) {
            char def_id[64];
            size_t dl = def.spans[0].len < sizeof(def_id) - 1 ? def.spans[0].len : sizeof(def_id) - 1;
            for (size_t i = 0; i < dl; i++) {
                def_id[i] = gap_at(gb, def.spans[0].start + i);
            }
            def_id[dl] = '\0';
            if (strcmp(id, def_id) == 0) {
                return false;  // Already exists
            }
        }
    }

    // Check if this is the first footnote definition
    bool first_footnote = true;
    for (size_t pos = 0; pos < len; pos++) {
        MdMatch2 def;
        if (md_check_footnote_def(gb, pos, &def)) {
            first_footnote = false;
            break;
        }
    }

    size_t insert_pos = len;

    // Ensure blank line before footnotes section
    if (len > 0 && gap_at(gb, len - 1) != '\n') {
        gap_insert(gb, insert_pos, '\n');
        insert_pos++;
    }
    gap_insert(gb, insert_pos, '\n');
    insert_pos++;

    // Add separator if this is the first footnote
    if (first_footnote) {
        const char *sep = "---\n\n";
        for (const char *p = sep; *p; p++) {
            gap_insert(gb, insert_pos, *p);
            insert_pos++;
        }
    }

    // Insert [^id]:
    gap_insert(gb, insert_pos, '[');
    insert_pos++;
    gap_insert(gb, insert_pos, '^');
    insert_pos++;
    for (const char *p = id; *p; p++) {
        gap_insert(gb, insert_pos, *p);
        insert_pos++;
    }
    gap_insert(gb, insert_pos, ']');
    insert_pos++;
    gap_insert(gb, insert_pos, ':');
    insert_pos++;
    gap_insert(gb, insert_pos, ' ');

    return true;
}

void footnote_maybe_create_at_cursor(GapBuffer *gb, size_t cursor) {
    if (cursor < 4) return;

    // Search backwards for a footnote reference near cursor
    for (size_t back = 3; back < 64 && back < cursor; back++) {
        size_t check_pos = cursor - back - 1;
        MdMatch ref;
        if (!md_check_footnote_ref(gb, check_pos, &ref)) continue;

        // Extract ID and create definition
        char id[64];
        size_t idl = ref.span.len < sizeof(id) - 1 ? ref.span.len : sizeof(id) - 1;
        for (size_t i = 0; i < idl; i++) {
            id[i] = gap_at(gb, ref.span.start + i);
        }
        id[idl] = '\0';

        footnote_create_definition(gb, id);
        return;
    }
}

// #endregion
