/* SPDX-License-Identifier: GPL-3.0-only
 * C/POSIX locale support for LLVM libc++ with the project's FreeBSD headers.
 * Named host locales are rejected. WiiCompiled keeps game languages in assets;
 * its C++ parsing/logging uses the classic locale. No FreeBSD private locale
 * objects are passed to the console's libc.
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <nl_types.h>
#include <runetype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <xlocale.h>

constexpr unsigned long Rune(int c) {
    if (c < 0 || c > 127) return 0;
    unsigned long flags = 0;
    const bool lower = c >= 'a' && c <= 'z', upper = c >= 'A' && c <= 'Z';
    const bool digit = c >= '0' && c <= '9';
    if (lower || upper) flags |= _CTYPE_A;
    if (lower) flags |= _CTYPE_L;
    if (upper) flags |= _CTYPE_U;
    if (digit) flags |= _CTYPE_D;
    if (c < 32 || c == 127) flags |= _CTYPE_C;
    if (c == ' ' || (c >= '\t' && c <= '\r')) flags |= _CTYPE_S;
    if (c == ' ' || c == '\t') flags |= _CTYPE_B;
    if (c >= 32 && c < 127) flags |= _CTYPE_R | _CTYPE_SW1;
    if (c > 32 && c < 127) flags |= _CTYPE_G;
    if (c > 32 && c < 127 && !lower && !upper && !digit) flags |= _CTYPE_P;
    if (digit) flags |= _CTYPE_X | (c - '0');
    if (c >= 'a' && c <= 'f') flags |= _CTYPE_X | (10 + c - 'a');
    if (c >= 'A' && c <= 'F') flags |= _CTYPE_X | (10 + c - 'A');
    return flags;
}
constexpr _RuneLocale MakeRunes() {
    _RuneLocale table{};
    for (int i = 0; i < 256; ++i) {
        table.__runetype[i] = Rune(i);
        table.__maplower[i] = i >= 'A' && i <= 'Z' ? i + 32 : i;
        table.__mapupper[i] = i >= 'a' && i <= 'z' ? i - 32 : i;
    }
    return table;
}
struct _xlocale { unsigned marker; };
static _xlocale classic{0x434c4f43};
static char empty[] = "", decimal[] = ".", classicName[] = "C";
static bool IsClassic(locale_t loc) { return loc == &classic || loc == LC_GLOBAL_LOCALE; }
static bool IsClassicName(const char* name) {
    return name && (!*name || !strcmp(name, "C") || !strcmp(name, "POSIX"));
}
extern "C" {
extern const _RuneLocale _DefaultRuneLocale = MakeRunes();
#undef _CurrentRuneLocale
const _RuneLocale* _CurrentRuneLocale = &_DefaultRuneLocale;
_Thread_local const _RuneLocale* _ThreadRuneLocale = nullptr;
_RuneLocale* __runes_for_locale(locale_t, int* cached) {
    if (cached) *cached = 1;
    return const_cast<_RuneLocale*>(&_DefaultRuneLocale);
}
unsigned long ___runetype_l(__ct_rune_t c, locale_t) { return Rune(c); }
__ct_rune_t ___tolower_l(__ct_rune_t c, locale_t) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
__ct_rune_t ___toupper_l(__ct_rune_t c, locale_t) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
int ___mb_cur_max_l(locale_t) { return 1; }
locale_t newlocale(int mask, const char* name, locale_t base) {
    if ((mask & ~LC_ALL_MASK) || (base && !IsClassic(base))) { errno = EINVAL; return nullptr; }
    if (!IsClassicName(name)) { errno = ENOENT; return nullptr; }
    return &classic;
}
int freelocale(locale_t loc) { if (!IsClassic(loc)) { errno = EINVAL; return -1; } return 0; }
locale_t duplocale(locale_t loc) { if (!IsClassic(loc)) { errno = EINVAL; return nullptr; } return &classic; }
locale_t uselocale(locale_t loc) { if (loc && !IsClassic(loc)) { errno = EINVAL; return nullptr; } return &classic; }
const char* querylocale(int mask, locale_t loc) { if ((mask & ~LC_ALL_MASK) || !IsClassic(loc)) { errno = EINVAL; return nullptr; } return classicName; }
char* setlocale(int category, const char* name) {
    if (category < LC_ALL || category >= _LC_LAST || (name && !IsClassicName(name))) { errno = EINVAL; return nullptr; }
    return classicName;
}
static lconv MakeConventions() {
    lconv v{};
    v.decimal_point = decimal;
    v.thousands_sep = v.grouping = v.int_curr_symbol = v.currency_symbol = empty;
    v.mon_decimal_point = v.mon_thousands_sep = v.mon_grouping = v.positive_sign = v.negative_sign = empty;
    v.int_frac_digits = v.frac_digits = v.p_cs_precedes = v.p_sep_by_space = CHAR_MAX;
    v.n_cs_precedes = v.n_sep_by_space = v.p_sign_posn = v.n_sign_posn = CHAR_MAX;
    v.int_p_cs_precedes = v.int_n_cs_precedes = v.int_p_sep_by_space = v.int_n_sep_by_space = CHAR_MAX;
    v.int_p_sign_posn = v.int_n_sign_posn = CHAR_MAX;
    return v;
}
lconv* localeconv_l(locale_t) { static lconv value = MakeConventions(); return &value; }
lconv* localeconv() { return localeconv_l(&classic); }
int iswctype_l(wint_t c, wctype_t type, locale_t) { return (Rune(static_cast<int>(c)) & type) != 0; }
int strcoll_l(const char* a, const char* b, locale_t) { return strcmp(a, b); }
int wcscoll_l(const wchar_t* a, const wchar_t* b, locale_t) { return wcscmp(a, b); }
size_t strxfrm_l(char* out, const char* in, size_t count, locale_t) {
    size_t length = strlen(in);
    if (count) { size_t n = length < count ? length : count; memcpy(out, in, n); if (n < count) out[n] = 0; }
    return length;
}
size_t wcsxfrm_l(wchar_t* out, const wchar_t* in, size_t count, locale_t) {
    size_t length = wcslen(in);
    if (count) { size_t n = length < count ? length : count; memcpy(out, in, n * sizeof(wchar_t)); if (n < count) out[n] = 0; }
    return length;
}
float strtof_l(const char* s, char** end, locale_t) { return strtof(s, end); }
double strtod_l(const char* s, char** end, locale_t) { return strtod(s, end); }
long double strtold_l(const char* s, char** end, locale_t) { return strtold(s, end); }
long long strtoll_l(const char* s, char** end, int base, locale_t) { return strtoll(s, end, base); }
unsigned long long strtoull_l(const char* s, char** end, int base, locale_t) { return strtoull(s, end, base); }
size_t strftime_l(char* out, size_t count, const char* format, const tm* time, locale_t) { return strftime(out, count, format, time); }
int snprintf_l(char* out, size_t count, locale_t, const char* format, ...) {
    va_list args; va_start(args, format); int rc = vsnprintf(out, count, format, args); va_end(args); return rc;
}
int sscanf_l(const char* in, locale_t, const char* format, ...) {
    va_list args; va_start(args, format); int rc = vsscanf(in, format, args); va_end(args); return rc;
}
int asprintf_l(char** out, locale_t, const char* format, ...) {
    *out = nullptr;
    va_list args, copy; va_start(args, format); va_copy(copy, args);
    int length = vsnprintf(nullptr, 0, format, copy); va_end(copy);
    if (length >= 0) {
        *out = static_cast<char*>(malloc(static_cast<size_t>(length) + 1));
        if (*out) vsnprintf(*out, static_cast<size_t>(length) + 1, format, args);
        else { errno = ENOMEM; length = -1; }
    }
    va_end(args); return length;
}
size_t mbrtowc_l(wchar_t* out, const char* s, size_t n, mbstate_t* state, locale_t) {
    if (state) memset(state, 0, sizeof(*state));
    if (!s) return 0;
    if (!n) return static_cast<size_t>(-2);
    unsigned char c = static_cast<unsigned char>(*s);
    if (c > 127) { errno = EILSEQ; return static_cast<size_t>(-1); }
    if (out) *out = c;
    return c ? 1 : 0;
}
size_t mbrlen_l(const char* s, size_t n, mbstate_t* state, locale_t loc) { return mbrtowc_l(nullptr, s, n, state, loc); }
int mbtowc_l(wchar_t* out, const char* s, size_t n, locale_t loc) { size_t rc = mbrtowc_l(out, s, n, nullptr, loc); return rc > 1 ? -1 : static_cast<int>(rc); }
size_t wcrtomb_l(char* out, wchar_t c, mbstate_t* state, locale_t) {
    if (state) memset(state, 0, sizeof(*state));
    if (!out) return 1;
    if (static_cast<unsigned>(c) > 127) { errno = EILSEQ; return static_cast<size_t>(-1); }
    *out = static_cast<char>(c); return 1;
}
wint_t btowc_l(int c, locale_t) { return c >= 0 && c <= 127 ? static_cast<wint_t>(c) : WEOF; }
int wctob_l(wint_t c, locale_t) { return c <= 127 ? static_cast<int>(c) : EOF; }
size_t mbsnrtowcs_l(wchar_t* out, const char** source, size_t bytes, size_t count, mbstate_t* state, locale_t loc) {
    const char* s = *source; size_t done = 0;
    while (bytes && (!out || done < count)) {
        wchar_t c; size_t rc = mbrtowc_l(&c, s, 1, state, loc);
        if (rc == static_cast<size_t>(-1)) { if (out) *source = s; return rc; }
        if (out) out[done] = c;
        if (!c) { if (out) *source = nullptr; return done; }
        ++done; ++s; --bytes;
    }
    if (out) *source = s;
    return done;
}
size_t mbsrtowcs_l(wchar_t* out, const char** s, size_t count, mbstate_t* state, locale_t loc) { return mbsnrtowcs_l(out, s, static_cast<size_t>(-1), count, state, loc); }
size_t wcsnrtombs_l(char* out, const wchar_t** source, size_t chars, size_t count, mbstate_t* state, locale_t loc) {
    const wchar_t* s = *source; size_t done = 0;
    while (chars && (!out || done < count)) {
        char c; size_t rc = wcrtomb_l(&c, *s, state, loc);
        if (rc == static_cast<size_t>(-1)) { if (out) *source = s; return rc; }
        if (out) out[done] = c;
        if (!c) { if (out) *source = nullptr; return done; }
        ++done; ++s; --chars;
    }
    if (out) *source = s;
    return done;
}
nl_catd catopen(const char*, int) { errno = ENOENT; return reinterpret_cast<nl_catd>(-1); }
char* catgets(nl_catd, int, int, const char* fallback) { return const_cast<char*>(fallback); }
int catclose(nl_catd) { errno = EBADF; return -1; }
}
