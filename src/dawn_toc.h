// dawn_toc.h

#ifndef DAWN_TOC_H
#define DAWN_TOC_H

#include "dawn_types.h"

// #region Constants

//! Maximum number of TOC entries
#define TOC_MAX_ENTRIES 256

//! Maximum length of header text
#define TOC_MAX_HEADER_LEN 128

// #endregion

// #region Types

//! A single TOC entry
typedef struct {
    size_t pos;                     //!< Byte position in document
    int32_t level;                      //!< Header level (1-6)
    int32_t depth;                      //!< Visual nesting depth (0-based, computed from hierarchy)
    char text[TOC_MAX_HEADER_LEN];  //!< Header text (without # prefix)
    int32_t text_len;                   //!< Length of header text
} TocEntry;

//! TOC state
typedef struct {
    TocEntry entries[TOC_MAX_ENTRIES];  //!< All headers
    int32_t count;                          //!< Number of headers
    int32_t *filtered;                      //!< Indices of filtered entries
    int32_t filtered_count;                 //!< Number of filtered entries
    int32_t selected;                       //!< Selected index in filtered list
    char filter[64];                    //!< Filter input
    int32_t filter_len;                     //!< Filter length
    int32_t filter_cursor;                  //!< Filter cursor position
    int32_t scroll;                         //!< Scroll offset
} TocState;

// #endregion

// #region TOC Operations

//! Build TOC from document
//! @param gb gap buffer containing document
//! @param state TOC state to populate
void toc_build(const GapBuffer *gb, TocState *state);

//! Apply filter to TOC entries
//! @param state TOC state to filter
void toc_filter(TocState *state);

//! Get currently selected entry (or NULL if none)
//! @param state TOC state
//! @return pointer to selected entry or NULL
const TocEntry *toc_get_selected(const TocState *state);

//! Initialize TOC state
//! @param state TOC state to initialize
void toc_init(TocState *state);

//! Free TOC state resources
//! @param state TOC state to free
void toc_free(TocState *state);

// #endregion

#endif // DAWN_TOC_H
