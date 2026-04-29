// dawn_settings.h - Persisted user preferences

#ifndef DAWN_SETTINGS_H
#define DAWN_SETTINGS_H

// #region Lifecycle

//! Load persisted settings into app state.
//! Reads <config_dir>/settings.json if present and applies any
//! recognized keys (theme, timer_mins) on top of existing defaults.
//! Missing or malformed files leave the defaults in place.
void settings_load(void);

//! Persist the current app preferences.
//! Writes theme and timer_mins to <config_dir>/settings.json.
//! Creates the config directory if it does not exist.
void settings_save(void);

// #endregion

#endif // DAWN_SETTINGS_H
