// dawn_date.h - Cross-platform ISO 8601 date parsing

#ifndef DAWN_DATE_H
#define DAWN_DATE_H

#include "dawn_backend.h"

//! Parse ISO 8601 date/time string
//! Handles: YYYY-MM-DD, YYYY-MM-DDTHH:MM:SS[.sss][Z|+HH:MM|-HH:MM]
//! @param str Input string to parse
//! @param out Output DawnDate structure
//! @return true on success, false on parse error
bool dawn_parse_iso_date(const char *str, DawnDate *out);

//! Format DawnDate to ISO 8601 string
//! Output format depends on fields: YYYY-MM-DD or YYYY-MM-DDTHH:MM:SS[.sss][tz]
//! @param d Input DawnDate structure
//! @param buf Output buffer
//! @param size Size of output buffer (recommend 32+ bytes)
void dawn_format_iso_date(const DawnDate *d, char *buf, size_t size);

//! Format DawnTime to ISO 8601 string with Z suffix
//! Output format: YYYY-MM-DDTHH:MM:SSZ
//! @param t Input DawnTime structure
//! @param buf Output buffer
//! @param size Size of output buffer (recommend 24+ bytes)
void dawn_format_iso_time(const DawnTime *t, char *buf, size_t size);

#endif // DAWN_DATE_H
