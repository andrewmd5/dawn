
#include "dawn_date.h"
#include "dawn_utils.h"
#include <stdint.h>
#include <string.h>
#include <time.h>

// #region Internal Declarations

static const unsigned char *conv_num(const unsigned char *, int *, unsigned int,
                                     unsigned int);
static const unsigned char *find_string(const unsigned char *, int *,
                                        const char *const *,
                                        const char *const *, int);

#define ALT_E 0x01
#define ALT_O 0x02
#define LEGAL_ALT(x) { if (alt_format & ~(x)) return NULL; }

#define TM_YEAR_BASE 1900

#define TM_SUNDAY 0
#define TM_MONDAY 1
#define TM_TUESDAY 2
#define TM_WEDNESDAY 3
#define TM_THURSDAY 4
#define TM_FRIDAY 5
#define TM_SATURDAY 6

#define S_YEAR (1 << 0)
#define S_MON (1 << 1)
#define S_YDAY (1 << 2)
#define S_MDAY (1 << 3)
#define S_WDAY (1 << 4)
#define S_HOUR (1 << 5)

#define HAVE_MDAY(s) (s & S_MDAY)
#define HAVE_MON(s) (s & S_MON)
#define HAVE_WDAY(s) (s & S_WDAY)
#define HAVE_YDAY(s) (s & S_YDAY)
#define HAVE_YEAR(s) (s & S_YEAR)
#define HAVE_HOUR(s) (s & S_HOUR)

#define SECSPERMIN 60
#define MINSPERHOUR 60
#define SECSPERHOUR (SECSPERMIN * MINSPERHOUR)
#define HOURSPERDAY 24

#define HERE_D_T_FMT "%a %b %e %H:%M:%S %Y"
#define HERE_D_FMT "%y/%m/%d"
#define HERE_T_FMT_AMPM "%I:%M:%S %p"
#define HERE_T_FMT "%H:%M:%S"

#define isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))
#define isleap_sum(a, b) isleap((a) % 400 + (b) % 400)

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#endif

#ifndef _WIN32
extern char *tzname[2];
#endif

// #endregion

// #region Data Tables

static const char *const nast[] = {"EST", "CST", "MST", "PST", "\0\0\0"};
static const char *const nadt[] = {"EDT", "CDT", "MDT", "PDT", "\0\0\0"};
static const char *weekday_name[] = {"Sunday",    "Monday",   "Tuesday",
                                     "Wednesday", "Thursday", "Friday",
                                     "Saturday"};
static const char *ab_weekday_name[] = {"Sun", "Mon", "Tue", "Wed",
                                        "Thu", "Fri", "Sat"};
static const char *month_name[] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December"};
static const char *ab_month_name[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *am_pm[] = {"AM", "PM"};

static const int start_of_month[2][13] = {
    {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
    {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366}};

// #endregion

// #region Helper Functions

static int first_wday_of(int yr) {
  return ((2 * (3 - (yr / 100) % 4)) + (yr % 100) + ((yr % 100) / 4) +
          (isleap(yr) ? 6 : 0) + 1) % 7;
}

#define delim(p) ((p) == '\0' || ISSPACE_(p))

static int fromzone(const unsigned char **bp, struct tm *tm, int mandatory) {
  char buf[512], *p;
  const unsigned char *rp;

  for (p = buf, rp = *bp; !delim(*rp) && p < &buf[sizeof(buf) - 1]; rp++)
    *p++ = *rp;
  *p = '\0';

  if (mandatory) *bp = rp;
  if (!ISALNUM_(*buf)) return 0;

  *bp = rp;
  tm->tm_isdst = 0;
  return 1;
}

// #endregion

// #region Core Parser

static char *parse_time(const char *buf, const char *fmt, struct tm *tm) {
  unsigned char c;
  const unsigned char *bp, *ep, *zname;
  int alt_format, i, split_year = 0, neg = 0, state = 0, day_offset = -1,
                     week_offset = 0, offs, mandatory;
  const char *new_fmt;

  bp = (const unsigned char *)buf;

  while (bp != NULL && (c = *fmt++) != '\0') {
    alt_format = 0;
    i = 0;

    if (ISSPACE_(c)) {
      while (ISSPACE_(*bp)) bp++;
      continue;
    }

    if (c != '%') goto literal;

  again:
    switch (c = *fmt++) {
    case '%':
    literal:
      if (c != *bp++) return NULL;
      LEGAL_ALT(0);
      continue;

    case 'E':
      LEGAL_ALT(0);
      alt_format |= ALT_E;
      goto again;

    case 'O':
      LEGAL_ALT(0);
      alt_format |= ALT_O;
      goto again;

    case 'c':
      new_fmt = HERE_D_T_FMT;
      state |= S_WDAY | S_MON | S_MDAY | S_YEAR;
      goto recurse;

    case 'F':
      new_fmt = "%Y-%m-%d";
      LEGAL_ALT(0);
      state |= S_MON | S_MDAY | S_YEAR;
      goto recurse;

    case 'R':
      new_fmt = "%H:%M";
      LEGAL_ALT(0);
      goto recurse;

    case 'r':
      new_fmt = HERE_T_FMT_AMPM;
      LEGAL_ALT(0);
      goto recurse;

    case 'X':
    case 'T':
      new_fmt = HERE_T_FMT;
      LEGAL_ALT(0);

    recurse:
      bp = (const unsigned char *)parse_time((const char *)bp, new_fmt, tm);
      LEGAL_ALT(ALT_E);
      continue;

    case 'x':
    case 'D': {
      new_fmt = HERE_D_FMT;
      LEGAL_ALT(0);
      state |= S_MON | S_MDAY | S_YEAR;
      const int year = split_year ? tm->tm_year : 0;

      bp = (const unsigned char *)parse_time((const char *)bp, new_fmt, tm);
      LEGAL_ALT(ALT_E);
      tm->tm_year += year;
      if (split_year && tm->tm_year % (2000 - TM_YEAR_BASE) <= 68)
        tm->tm_year -= 2000 - TM_YEAR_BASE;
      split_year = 1;
      continue;
    }

    case 'A':
    case 'a':
      bp = find_string(bp, &tm->tm_wday, weekday_name, ab_weekday_name, 7);
      LEGAL_ALT(0);
      state |= S_WDAY;
      continue;

    case 'B':
    case 'b':
    case 'h':
      bp = find_string(bp, &tm->tm_mon, month_name, ab_month_name, 12);
      LEGAL_ALT(0);
      state |= S_MON;
      continue;

    case 'C':
      i = 20;
      bp = conv_num(bp, &i, 0, 99);
      i = i * 100 - TM_YEAR_BASE;
      if (split_year) i += tm->tm_year % 100;
      split_year = 1;
      tm->tm_year = i;
      LEGAL_ALT(ALT_E);
      state |= S_YEAR;
      continue;

    case 'd':
    case 'e':
      bp = conv_num(bp, &tm->tm_mday, 1, 31);
      LEGAL_ALT(ALT_O);
      state |= S_MDAY;
      continue;

    case 'k':
      LEGAL_ALT(0);
      /* FALLTHROUGH */
    case 'H':
      bp = conv_num(bp, &tm->tm_hour, 0, 23);
      LEGAL_ALT(ALT_O);
      state |= S_HOUR;
      continue;

    case 'l':
      LEGAL_ALT(0);
      /* FALLTHROUGH */
    case 'I':
      bp = conv_num(bp, &tm->tm_hour, 1, 12);
      if (tm->tm_hour == 12) tm->tm_hour = 0;
      LEGAL_ALT(ALT_O);
      state |= S_HOUR;
      continue;

    case 'j':
      i = 1;
      bp = conv_num(bp, &i, 1, 366);
      tm->tm_yday = i - 1;
      LEGAL_ALT(0);
      state |= S_YDAY;
      continue;

    case 'M':
      bp = conv_num(bp, &tm->tm_min, 0, 59);
      LEGAL_ALT(ALT_O);
      continue;

    case 'm':
      i = 1;
      bp = conv_num(bp, &i, 1, 12);
      tm->tm_mon = i - 1;
      LEGAL_ALT(ALT_O);
      state |= S_MON;
      continue;

    case 'p':
      bp = find_string(bp, &i, am_pm, NULL, 2);
      if (HAVE_HOUR(state) && tm->tm_hour > 11) return NULL;
      tm->tm_hour += i * 12;
      LEGAL_ALT(0);
      continue;

    case 'S':
      bp = conv_num(bp, &tm->tm_sec, 0, 61);
      LEGAL_ALT(ALT_O);
      continue;

#ifndef TIME_MAX
#define TIME_MAX INT64_MAX
#endif
    case 's': {
      time_t sse = 0;
      uint64_t rulim = TIME_MAX;

      if (*bp < '0' || *bp > '9') { bp = NULL; continue; }

      do {
        sse *= 10;
        sse += *bp++ - '0';
        rulim /= 10;
      } while ((sse * 10 <= TIME_MAX) && rulim && *bp >= '0' && *bp <= '9');

      if (sse < 0 || (uint64_t)sse > TIME_MAX) { bp = NULL; continue; }

#ifdef _WIN32
      if (localtime_s(tm, &sse) == 0)
#else
      if (localtime_r(&sse, tm))
#endif
        state |= S_YDAY | S_WDAY | S_MON | S_MDAY | S_YEAR;
      else
        bp = NULL;
    }
      continue;

    case 'U':
    case 'W':
      bp = conv_num(bp, &i, 0, 53);
      LEGAL_ALT(ALT_O);
      day_offset = (c == 'U') ? TM_SUNDAY : TM_MONDAY;
      week_offset = i;
      continue;

    case 'w':
      bp = conv_num(bp, &tm->tm_wday, 0, 6);
      LEGAL_ALT(ALT_O);
      state |= S_WDAY;
      continue;

    case 'u':
      bp = conv_num(bp, &i, 1, 7);
      tm->tm_wday = i % 7;
      LEGAL_ALT(ALT_O);
      state |= S_WDAY;
      continue;

    case 'g':
      bp = conv_num(bp, &i, 0, 99);
      continue;

    case 'G':
      do bp++; while (ISDIGIT_(*bp));
      continue;

    case 'V':
      bp = conv_num(bp, &i, 0, 53);
      continue;

    case 'Y':
      i = TM_YEAR_BASE;
      bp = conv_num(bp, &i, 0, 9999);
      tm->tm_year = i - TM_YEAR_BASE;
      LEGAL_ALT(ALT_E);
      state |= S_YEAR;
      continue;

    case 'y':
      bp = conv_num(bp, &i, 0, 99);
      if (split_year)
        i += (tm->tm_year / 100) * 100;
      else {
        split_year = 1;
        if (i <= 68) i = i + 2000 - TM_YEAR_BASE;
      }
      tm->tm_year = i;
      state |= S_YEAR;
      continue;

    case 'Z':
    case 'z':
#ifdef _WIN32
      _tzset();
#else
      tzset();
#endif
      mandatory = c == 'z';

      if (mandatory) while (ISSPACE_(*bp)) bp++;

      zname = bp;
      switch (*bp++) {
      case 'G':
        if (*bp++ != 'M') goto namedzone;
        /*FALLTHROUGH*/
      case 'U':
        if (*bp++ != 'T') goto namedzone;
        else if (!delim(*bp) && *bp++ != 'C') goto namedzone;
        /*FALLTHROUGH*/
      case 'Z':
        if (!delim(*bp)) goto namedzone;
        tm->tm_isdst = 0;
        continue;
      case '+':
        neg = 0;
        break;
      case '-':
        neg = 1;
        break;
      default:
      namedzone:
        bp = zname;

        if (delim(bp[1]) && ((*bp >= 'A' && *bp <= 'I') || (*bp >= 'L' && *bp <= 'Y'))) {
          bp++;
          continue;
        }
        if (delim(bp[1]) && *bp == 'J') {
          bp++;
          continue;
        }

        if (delim(bp[0]) || delim(bp[1]) || delim(bp[2]) || !delim(bp[3]))
          goto loadzone;
        ep = find_string(bp, &i, nast, NULL, 4);
        if (ep != NULL) { bp = ep; continue; }
        ep = find_string(bp, &i, nadt, NULL, 4);
        if (ep != NULL) { tm->tm_isdst = 1; bp = ep; continue; }
#ifdef _WIN32
        ep = find_string(bp, &i, (const char *const *)_tzname, NULL, 2);
#else
        ep = find_string(bp, &i, (const char *const *)tzname, NULL, 2);
#endif
        if (ep != NULL) { tm->tm_isdst = i; bp = ep; continue; }
      loadzone:
        if (fromzone(&bp, tm, mandatory)) continue;
        goto out;
      }
      offs = 0;
      for (i = 0; i < 4;) {
        if (ISDIGIT_(*bp)) {
          offs = offs * 10 + (*bp++ - '0');
          i++;
          continue;
        }
        if (i == 2 && *bp == ':') { bp++; continue; }
        break;
      }
      if (ISDIGIT_(*bp)) goto out;
      switch (i) {
      case 2:
        offs *= SECSPERHOUR;
        break;
      case 4:
        i = offs % 100;
        offs /= 100;
        if (i >= SECSPERMIN) goto out;
        offs = offs * SECSPERHOUR + i * SECSPERMIN;
        break;
      default:
      out:
        if (mandatory) return NULL;
        bp = zname;
        continue;
      }
      if (offs >= (HOURSPERDAY * SECSPERHOUR)) goto out;
      if (neg) offs = -offs;
      tm->tm_isdst = 0;
      continue;

    case 'n':
    case 't':
      while (ISSPACE_(*bp)) bp++;
      LEGAL_ALT(0);
      continue;

    default:
      return NULL;
    }
  }

  if (!HAVE_YDAY(state) && HAVE_YEAR(state)) {
    if (HAVE_MON(state) && HAVE_MDAY(state)) {
      tm->tm_yday = start_of_month[isleap_sum(tm->tm_year, TM_YEAR_BASE)][tm->tm_mon] +
          (tm->tm_mday - 1);
      state |= S_YDAY;
    } else if (day_offset != -1) {
      if (!HAVE_WDAY(state)) {
        tm->tm_wday = day_offset;
        state |= S_WDAY;
      }
      tm->tm_yday = (7 - first_wday_of(tm->tm_year + TM_YEAR_BASE) + day_offset) % 7 +
          (week_offset - 1) * 7 + tm->tm_wday - day_offset;
      state |= S_YDAY;
    }
  }

  if (HAVE_YDAY(state) && HAVE_YEAR(state)) {
    int isleap_val;

    if (!HAVE_MON(state)) {
      i = 0;
      isleap_val = isleap_sum(tm->tm_year, TM_YEAR_BASE);
      while (tm->tm_yday >= start_of_month[isleap_val][i]) i++;
      if (i > 12) {
        i = 1;
        tm->tm_yday -= start_of_month[isleap_val][12];
        tm->tm_year++;
      }
      tm->tm_mon = i - 1;
      state |= S_MON;
    }

    if (!HAVE_MDAY(state)) {
      isleap_val = isleap_sum(tm->tm_year, TM_YEAR_BASE);
      tm->tm_mday = tm->tm_yday - start_of_month[isleap_val][tm->tm_mon] + 1;
      state |= S_MDAY;
    }

    if (!HAVE_WDAY(state)) {
      i = 0;
      week_offset = first_wday_of(tm->tm_year);
      while (i++ <= tm->tm_yday) {
        if (week_offset++ >= 6) week_offset = 0;
      }
      tm->tm_wday = week_offset;
      state |= S_WDAY;
    }
  }

  return (char *)bp;
}

static const unsigned char *conv_num(const unsigned char *buf, int *dest,
                                     unsigned int llim, unsigned int ulim) {
  unsigned int result = 0;
  unsigned int rulim = ulim;
  unsigned char ch = *buf;

  if (ch < '0' || ch > '9') return NULL;

  do {
    result *= 10;
    result += ch - '0';
    rulim /= 10;
    ch = *++buf;
  } while ((result <= ulim) && rulim && ch >= '0' && ch <= '9');

  if (result < llim || result > ulim) return NULL;

  *dest = result;
  return buf;
}

static const unsigned char *find_string(const unsigned char *bp, int *tgt,
                                        const char *const *n1,
                                        const char *const *n2, int c) {
  int i;
  size_t len;

  for (; n1 != NULL; n1 = n2, n2 = NULL) {
    for (i = 0; i < c; i++, n1++) {
      len = strlen(*n1);
      if (strncasecmp(*n1, (const char *)bp, len) == 0) {
        *tgt = i;
        return bp + len;
      }
    }
  }

  return NULL;
}

// #endregion

// #region Public API

bool dawn_parse_iso_date(const char *str, DawnDate *out) {
    if (!str || !out) return false;
    memset(out, 0, sizeof(*out));

    struct tm tm;
    memset(&tm, 0, sizeof(tm));

    // Try full datetime: YYYY-MM-DDTHH:MM:SS
    const char *p = parse_time(str, "%Y-%m-%dT%H:%M:%S", &tm);
    if (p) {
        out->has_time = true;

        // Milliseconds (.NNN) - not supported by strptime
        if (*p == '.') {
            p++;
            uint16_t ms = 0;
            for (int i = 0; i < 3 && ISDIGIT_(*p); i++, p++)
                ms = ms * 10 + (uint16_t)(*p - '0');
            while (ISDIGIT_(*p)) p++;
            out->ms = ms;
        }

        // Timezone via %z
        if (*p == 'Z' || *p == '+' || *p == '-') {
            const char *tz_start = p;
            p = parse_time(p, "%z", &tm);
            if (p) {
                out->has_tz = true;
                size_t len = (size_t)(p - tz_start);
                if (len >= sizeof(out->tz)) len = sizeof(out->tz) - 1;
                memcpy(out->tz, tz_start, len);
                out->tz[len] = '\0';
            }
        }
    } else {
        // Date only: YYYY-MM-DD
        p = parse_time(str, "%Y-%m-%d", &tm);
        if (!p) return false;
    }

    out->year = (int16_t)(tm.tm_year + TM_YEAR_BASE);
    out->mon = (uint8_t)(tm.tm_mon + 1);
    out->mday = (uint8_t)tm.tm_mday;
    out->hour = (uint8_t)tm.tm_hour;
    out->min = (uint8_t)tm.tm_min;
    out->sec = (uint8_t)tm.tm_sec;
    return true;
}

void dawn_format_iso_date(const DawnDate *d, char *buf, size_t size) {
    if (!d || !buf || size == 0) return;

    if (d->has_time) {
        if (d->ms > 0 && d->has_tz) {
            snprintf(buf, size, "%04d-%02d-%02dT%02d:%02d:%02d.%03d%s",
                     d->year, d->mon, d->mday,
                     d->hour, d->min, d->sec, d->ms, d->tz);
        } else if (d->ms > 0) {
            snprintf(buf, size, "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
                     d->year, d->mon, d->mday,
                     d->hour, d->min, d->sec, d->ms);
        } else if (d->has_tz) {
            snprintf(buf, size, "%04d-%02d-%02dT%02d:%02d:%02d%s",
                     d->year, d->mon, d->mday,
                     d->hour, d->min, d->sec, d->tz);
        } else {
            snprintf(buf, size, "%04d-%02d-%02dT%02d:%02d:%02d",
                     d->year, d->mon, d->mday,
                     d->hour, d->min, d->sec);
        }
    } else {
        snprintf(buf, size, "%04d-%02d-%02d", d->year, d->mon, d->mday);
    }
}

void dawn_format_iso_time(const DawnTime *t, char *buf, size_t size) {
    if (!t || !buf || size == 0) return;
    snprintf(buf, size, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             t->year, t->mon + 1, t->mday, t->hour, t->min, t->sec);
}

// #endregion
