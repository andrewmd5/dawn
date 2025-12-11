// dawn_timer.h

#ifndef DAWN_TIMER_H
#define DAWN_TIMER_H

#include "dawn_types.h"

// #region Timer Operations

//! Get remaining time in seconds
//! @return seconds remaining, 0 if timer expired or not running
int32_t timer_remaining(void);

//! Check if timer has expired and handle completion
//! Transitions to MODE_FINISHED and saves session when timer ends
void timer_check(void);

//! Toggle timer pause state
//! Does nothing if timer is not running or is unlimited
void timer_toggle_pause(void);

//! Add minutes to timer
//! @param mins minutes to add (can start a new timer if none running)
void timer_add_minutes(int32_t mins);

// #endregion

#endif // DAWN_TIMER_H
