// dawn_render.h

#ifndef DAWN_RENDER_H
#define DAWN_RENDER_H

#include "dawn_types.h"

// #region Utility Functions

//! Clear entire screen with background color
void render_clear(void);

//! Print centered text at given row
//! @param row display row (1-based)
//! @param text text to display
//! @param fg foreground color
void render_center_text(int32_t row, const char *text, DawnColor fg);

//! Render a floating popup box centered on screen
//! @param width box width in columns
//! @param height box height in rows
//! @param out_top output: top-left row (1-based)
//! @param out_left output: top-left column (1-based)
void render_popup_box(int32_t width, int32_t height, int32_t *out_top, int32_t *out_left);

// #endregion

// #region Screen Renderers

//! Render the welcome/menu screen
void render_welcome(void);

//! Render the timer selection screen
void render_timer_select(void);

//! Render the style selection screen
void render_style_select(void);

//! Render the help screen with keyboard shortcuts
void render_help(void);

//! Render the session history browser
void render_history(void);

//! Render the session completion screen
void render_finished(void);

//! Render the frontmatter editing screen
void render_fm_edit(void);

//! Render the block editing screen (images, etc.)
void render_block_edit(void);

//! Render the table of contents overlay
void render_toc(void);

//! Render the search overlay
void render_search(void);

// #endregion

#endif // DAWN_RENDER_H
