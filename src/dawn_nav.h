// dawn_nav.h

#ifndef DAWN_NAV_H
#define DAWN_NAV_H

#include "dawn_types.h"

// #region Line Navigation

//! Find start of line containing position
//! @param pos byte position in text
//! @return position of first character on line
size_t nav_line_start(size_t pos);

//! Find end of line containing position
//! @param pos byte position in text
//! @return position after last character on line (before newline)
size_t nav_line_end(size_t pos);

//! Move cursor up or down by lines
//! @param pos current byte position
//! @param delta lines to move (negative = up, positive = down)
//! @return new cursor position, preserving column offset
size_t nav_move_line(size_t pos, int32_t delta);

//! Move cursor up or down by visual (wrapped) lines
//! @param pos current byte position
//! @param delta lines to move (negative = up, positive = down)
//! @param text_width width for word wrapping
//! @return new cursor position
size_t nav_move_visual_line(size_t pos, int32_t delta, int32_t text_width);

//! Move cursor up or down, skipping over block elements (tables, code blocks, images)
//! @param pos current byte position
//! @param delta lines to move (negative = up, positive = down)
//! @param text_width width for word wrapping
//! @param skip_blocks if true, skip over block elements; if false, behave like nav_move_visual_line
//! @return new cursor position
size_t nav_move_visual_line_block_aware(size_t pos, int32_t delta, int32_t text_width, bool skip_blocks);

//! Skip forward out of current block element
//! @param pos current byte position
//! @return position after block, or unchanged if not in block
size_t nav_skip_block_forward(size_t pos);

//! Skip backward out of current block element
//! @param pos current byte position
//! @return position before block, or unchanged if not in block
size_t nav_skip_block_backward(size_t pos);

// #endregion

// #region Word Navigation

//! Move to start of previous word
//! @param pos current byte position
//! @return position at start of word
size_t nav_word_left(size_t pos);

//! Move to start of next word
//! @param pos current byte position
//! @return position at start of next word
size_t nav_word_right(size_t pos);

// #endregion

// #region Selection

//! Get current selection range (normalized start < end)
//! @param start output: selection start position
//! @param end output: selection end position
void get_selection(size_t *start, size_t *end);

//! Check if there is an active selection
//! @return true if selection exists and is non-empty
bool has_selection(void);

// #endregion

#endif // DAWN_NAV_H
