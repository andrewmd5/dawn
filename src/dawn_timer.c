// dawn_timer.c

#include "dawn_timer.h"
#include "dawn_file.h"

// #region Timer Operations

int32_t timer_remaining(void) {
    if (!app.timer_on || app.timer_mins == 0) return app.timer_mins * 60;
    if (app.timer_paused) return (int32_t)app.timer_paused_at;

    int64_t now = DAWN_BACKEND(app)->clock(DAWN_CLOCK_SEC);
    int64_t elapsed = now - app.timer_start;
    int32_t left = app.timer_mins * 60 - (int32_t)elapsed;
    return left > 0 ? left : 0;
}

void timer_check(void) {
    if (app.timer_on && !app.timer_paused && app.timer_mins > 0 && timer_remaining() == 0) {
        app.timer_on = false;
        app.timer_done = true;
        app.mode = MODE_FINISHED;
        save_session();
    }
}

void timer_toggle_pause(void) {
    if (!app.timer_on || app.timer_mins == 0) return;

    int64_t now = DAWN_BACKEND(app)->clock(DAWN_CLOCK_SEC);

    if (app.timer_paused) {
        // Resume: adjust start time to account for pause duration
        app.timer_start = now - (app.timer_mins * 60 - app.timer_paused_at);
        app.timer_paused = false;
    } else {
        // Pause: save remaining time
        app.timer_paused_at = timer_remaining();
        app.timer_paused = true;
    }
}

void timer_add_minutes(int32_t mins) {
    int64_t now = DAWN_BACKEND(app)->clock(DAWN_CLOCK_SEC);

    if (!app.timer_on) {
        // Start new timer
        app.timer_mins = mins;
        app.timer_start = now;
        app.timer_on = true;
        app.timer_paused = false;
    } else if (app.timer_paused) {
        // Add to paused time
        app.timer_paused_at += mins * 60;
    } else {
        // Extend running timer by shifting start time back
        app.timer_start += mins * 60;
    }
}

// #endregion
