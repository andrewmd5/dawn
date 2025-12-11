// dawn_types.h

#ifndef DAWN_TYPES_H
#define DAWN_TYPES_H

// #region Standard Library Includes (Platform-Independent)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

// #endregion

// #region Backend Abstraction

#include "dawn_backend.h"

// #endregion

// #region External Dependencies

#include "utf8proc/utf8proc.h"

//! Apple Intelligence integration (macOS 26.0+)
#ifdef USE_LIBAI
    #include "ai.h"
    #include "search.h"
    #define HAS_LIBAI 1
#else
    #define HAS_LIBAI 0
#endif

// #endregion

// #region Application Configuration

#define APP_NAME "dawn"
#define APP_TAGLINE "Draft Anything, Write Now"
#ifndef VERSION
#define VERSION "dev"
#endif

//! Maximum document size (1MB)
#define MAX_TEXT_SIZE (1024 * 1024)

//! Directory name for storing sessions in user's home
#define HISTORY_DIR_NAME ".dawn"

//! Default writing timer duration
#define DEFAULT_TIMER_MINUTES 15

//! Gap buffer initial gap size
#define GAP_BUFFER_GAP_SIZE 1024

//! AI chat panel width in columns
#define AI_PANEL_WIDTH 45

//! Maximum AI response size
#define MAX_AI_RESPONSE (64 * 1024)

//! Maximum AI input size
#define MAX_AI_INPUT 4096

//! Maximum lines in AI input area
#define AI_INPUT_MAX_LINES 6

//! Timer preset options (minutes)
static const int32_t TIMER_PRESETS[] = { 0, 5, 10, 15, 20, 25, 30 };
#define NUM_PRESETS (sizeof(TIMER_PRESETS) / sizeof(TIMER_PRESETS[0]))

// #endregion

// #region Core Types

//! Gap buffer for efficient text editing
//! @see dawn_gap.h for operations
typedef struct {
    char *buffer;
    size_t buffer_size;
    size_t gap_start;
    size_t gap_end;
} GapBuffer;

//! Application mode/screen
DAWN_ENUM(uint8_t) {
    MODE_WELCOME,       //!< Start screen
    MODE_WRITING,       //!< Main editor
    MODE_TIMER_SELECT,  //!< Timer configuration
    MODE_HISTORY,       //!< Document history browser
    MODE_STYLE,         //!< Style selection (unused)
    MODE_FINISHED,      //!< Timer completed screen
    MODE_TITLE_EDIT,    //!< Document title editor (modal)
    MODE_HELP,          //!< Keyboard shortcuts help
    MODE_BLOCK_EDIT,    //!< Block editor (modal) - images, etc.
    MODE_TOC,           //!< Table of contents navigation (modal)
    MODE_SEARCH         //!< Document search (modal)
} AppMode;

//! Push a modal mode (saves current mode for later restoration)
#define MODE_PUSH(new_mode) do { app.prev_mode = app.mode; app.mode = (new_mode); } while(0)

//! Pop back to previous mode
#define MODE_POP() do { app.mode = app.prev_mode; } while(0)

//! DawnColor theme
DAWN_ENUM(uint8_t) {
    THEME_LIGHT,
    THEME_DARK
} Theme;

//! Writing style (visual presentation)
DAWN_ENUM(uint8_t) {
    STYLE_MINIMAL,      //!< Clean, minimal UI
    STYLE_TYPEWRITER,   //!< Monospace feel
    STYLE_ELEGANT       //!< Italic, refined
} WritingStyle;

//! AI chat message
typedef struct {
    char *text;
    size_t len;
    bool is_user;       //!< true = user message, false = AI response
} ChatMessage;

//! History entry for saved documents
typedef struct {
    char *path;         //!< Full path to .md file
    char *title;        //!< Document title from frontmatter
    char *date_str;     //!< Formatted modification date
} HistoryEntry;

// #endregion


// #region Application State

//! Global application state
typedef struct {
    // Backend context
    DawnCtx ctx;

    // Document
    GapBuffer text;     //!< Document content
    size_t cursor;      //!< Cursor position in text

    // Selection
    bool selecting;     //!< Selection mode active
    size_t sel_anchor;  //!< Selection start position

    // Viewport
    int32_t scroll_y;       //!< Vertical scroll offset

    // Timer
    int32_t timer_mins;     //!< Timer duration in minutes
    int64_t timer_start; //!< Timer start timestamp
    int64_t timer_paused_at; //!< Pause timestamp
    bool timer_on;      //!< Timer running
    bool timer_paused;  //!< Timer paused
    bool timer_done;    //!< Timer completed

    // UI State
    AppMode mode;       //!< Current screen/mode
    AppMode prev_mode;  //!< Previous mode (for modal return)
    Theme theme;        //!< DawnColor theme
    WritingStyle style; //!< Writing style
    int32_t preset_idx;     //!< Selected timer preset index
    bool focus_mode;    //!< Focus mode enabled
    bool plain_mode;    //!< Plain text mode (no WYSIWYG rendering)
    bool preview_mode;  //!< Read-only preview mode

    // Display
    int32_t rows, cols;     //!< Display dimensions

    // History
    HistoryEntry *history;  //!< Document history array
    int32_t hist_count;         //!< Number of history entries
    int32_t hist_sel;           //!< Selected history index

    // Current Session
    char *session_path;     //!< Path to current document
    char *session_title;    //!< Document title
    char title_edit_buf[256]; //!< Title edit buffer
    size_t title_edit_len;    //!< Title edit length
    size_t title_edit_cursor; //!< Title edit cursor

    // Block Edit State
    struct {
        int8_t type;          //!< BlockType being edited
        size_t pos;           //!< Position of block in text
        size_t len;           //!< Total length of block syntax
        int32_t field;        //!< Current field index
        union {
            struct {
                char alt[256];
                char title[256];
                char width[16];
                char height[16];
                size_t alt_len;
                size_t title_len;
                size_t width_len;
                size_t height_len;
                bool width_pct;
                bool height_pct;
            } image;
            // Future: struct { ... } code;
            // Future: struct { ... } link;
        };
    } block_edit;

    // AI Chat
    bool ai_open;           //!< AI panel visible
    bool ai_focused;        //!< AI input focused
    char ai_input[MAX_AI_INPUT]; //!< AI input buffer
    size_t ai_input_len;    //!< AI input length
    size_t ai_input_cursor; //!< AI input cursor
    ChatMessage *chat_msgs; //!< Chat history
    int32_t chat_count;         //!< Number of messages
    int32_t chat_scroll;        //!< Chat scroll offset
    bool ai_thinking;       //!< AI processing request

    #if HAS_LIBAI
    ai_context_t *ai_ctx;       //!< libai context
    ai_session_id_t ai_session; //!< libai session
    #endif
    bool ai_ready;          //!< AI available

    // Undo/Redo
    #define MAX_UNDO 100
    struct {
        char *text;         //!< Saved text state
        size_t text_len;    //!< Length of saved text
        size_t cursor;      //!< Cursor position
    } undo_stack[MAX_UNDO];
    int32_t undo_count;         //!< Number of undo states
    int32_t undo_pos;           //!< Current position in undo stack

    // State flags
    bool resize_needed;     //!< Display resize pending
    bool quit;              //!< Quit requested
    bool hide_cursor_syntax;//!< When true, don't show raw syntax when cursor is over markdown elements

    // Auto-save
    int64_t last_save_time;  //!< Last auto-save timestamp

    // Block cache (forward declared, allocated on demand)
    void *block_cache;      //!< BlockCache* - block-based document model

    // Syntax highlighting
    void *hl_ctx;           //!< hl_ctx_t* - syntax highlight context

    // TOC and Search (forward declared, allocated on demand)
    void *toc_state;        //!< TocState* - table of contents state
    void *search_state;     //!< SearchState* - document search state
} App;

//! Global application instance
extern App app;

// #endregion

#endif // DAWN_TYPES_H
