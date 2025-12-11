// dawn_footnote.h

#ifndef DAWN_FOOTNOTE_H
#define DAWN_FOOTNOTE_H

#include "dawn_types.h"
#include "dawn_gap.h"

// #region Footnote Navigation

//! Jump to footnote definition from reference, or vice versa
//! If on a reference [^id], jumps to its definition
//! If on a definition [^id]:, jumps to first reference
//! If definition doesn't exist, creates it at end of document
//! @param gb gap buffer containing text
//! @param cursor current cursor position (updated on jump)
void footnote_jump(GapBuffer *gb, size_t *cursor);

//! Create a footnote definition at end of document
//! Called when user completes typing a footnote reference [^id]
//! @param gb gap buffer containing text
//! @param id the footnote identifier (without [^ and ])
//! @return true if definition was created
bool footnote_create_definition(GapBuffer *gb, const char *id);

//! Check if cursor just completed a footnote reference and create definition if needed
//! Called after user types ']' to auto-create missing footnote definitions
//! @param gb gap buffer containing text
//! @param cursor current cursor position
void footnote_maybe_create_at_cursor(GapBuffer *gb, size_t cursor);

// #endregion

#endif // DAWN_FOOTNOTE_H
