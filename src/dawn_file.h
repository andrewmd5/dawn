// dawn_file.h

#ifndef DAWN_FILE_H
#define DAWN_FILE_H

#include "dawn_types.h"

// #region History Directory

//! Get path to history/sessions directory
//! @return static buffer with path to ~/.dawn
char *history_dir(void);

// #endregion

// #region Session Persistence

//! Save current session to disk
//! Creates new file if first save, writes markdown with YAML frontmatter
//! Also saves AI chat history to companion .chat.json file
void save_session(void);

//! Load list of past sessions from history directory
//! Populates app.history array, sorted newest first
void load_history(void);

//! Load AI chat history for a session
//! @param session_path path to the .md session file
void load_chat_history(const char *session_path);

// #endregion

// #region File Operations

//! Load a file for editing, parsing frontmatter
//! @param path path to the .md file to open
void load_file_for_editing(const char *path);

//! Load content from buffer for editing, parsing frontmatter
//! @param content buffer containing markdown content
//! @param size size of content buffer
void load_buffer_for_editing(const char *content, size_t size);

//! Reveal a file in system file manager
//! @param path path to the file to reveal
void open_in_finder(const char *path);

// #endregion

#endif // DAWN_FILE_H
