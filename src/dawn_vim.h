// dawn_vim.h - Neovim-style modal editing for Dawn

#ifndef DAWN_VIM_H
#define DAWN_VIM_H

#include "dawn_types.h"

// #region Core

//! Handle a key when vim mode is enabled.
//! @param key key code from input layer
//! @return true if key was consumed (caller should skip default handling)
bool vim_handle_key(int32_t key);

//! Short label for current vim mode (for status bar)
const char* vim_mode_label(void);

//! True when vim modal handling is active (enabled and not in plain insert)
bool vim_active(void);

//! Enter insert mode (from normal/visual/command)
void vim_enter_insert(void);

//! Enter normal mode, clearing pending state and visual selection as needed
void vim_enter_normal(void);

//! Clamp cursor so normal mode never rests past end-of-line
void vim_clamp_cursor(void);

// #endregion

// #region Motions (exposed for reuse by operators)

//! Vim 'w': start of next word (word = alnum + '_')
size_t vim_word_forward(size_t pos, int32_t count);
//! Vim 'W': start of next WORD (whitespace-separated)
size_t vim_WORD_forward(size_t pos, int32_t count);
//! Vim 'b': start of current/previous word
size_t vim_word_back(size_t pos, int32_t count);
//! Vim 'B': start of current/previous WORD
size_t vim_WORD_back(size_t pos, int32_t count);
//! Vim 'e': end of next word
size_t vim_word_end(size_t pos, int32_t count);
//! Vim 'E': end of next WORD
size_t vim_WORD_end(size_t pos, int32_t count);
//! Find char on current line (f/F/t/T semantics)
size_t vim_find_char(size_t pos, char target, bool forward, bool till, int32_t count);
//! Match brackets (), [], {} (vim %)
size_t vim_match_bracket(size_t pos);
//! Start of next/prev paragraph ({ / })
size_t vim_paragraph_forward(size_t pos, int32_t count);
size_t vim_paragraph_back(size_t pos, int32_t count);

// #endregion

#endif // DAWN_VIM_H
