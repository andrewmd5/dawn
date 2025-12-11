// dawn_app.h
//! Frontends (terminal, web, etc.) call these functions

#ifndef DAWN_APP_H
#define DAWN_APP_H

#include "dawn_types.h"

// #region Engine Lifecycle

//! Initialize the Dawn engine
//! Must be called after platform_init() has been called by the frontend
//! @param theme color theme to use
//! @return true on success
bool dawn_engine_init(Theme theme);

//! Shutdown the Dawn engine
//! Frontend should call platform_shutdown() after this
void dawn_engine_shutdown(void);

// #endregion

// #region Main Loop

//! Process one frame of the application
//! Handles resize, timer, autosave, render, and input
//! @return true if app should continue, false if quitting
bool dawn_frame(void);

//! Request the application to quit
void dawn_request_quit(void);

//! Check if quit was requested
bool dawn_should_quit(void);

// #endregion

// #region Document Operations

//! Load a document from path
//! @param path path to markdown file
//! @return true on success
bool dawn_load_document(const char *path);

//! Load a document in preview (read-only) mode
//! @param path path to markdown file
//! @return true on success
bool dawn_preview_document(const char *path);

//! Print a document (render linearly and exit)
//! @param path path to markdown file
//! @return true on success
bool dawn_print_document(const char *path);

//! Print a buffer (render linearly and exit)
//! @param content buffer containing markdown content
//! @param size size of content buffer
//! @return true on success
bool dawn_print_buffer(const char *content, size_t size);

//! Preview a buffer in read-only mode
//! @param content buffer containing markdown content
//! @param size size of content buffer
//! @return true on success
bool dawn_preview_buffer(const char *content, size_t size);

//! Start a new empty document
void dawn_new_document(void);

//! Save the current document
void dawn_save_document(void);

// #endregion

// #region Display

//! Update display dimensions from platform
//! Call this when the platform reports a resize
void dawn_update_size(void);

//! Render the current frame
//! Called automatically by dawn_frame(), but can be called directly
void dawn_render(void);

// #endregion

#endif // DAWN_APP_H
