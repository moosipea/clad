#ifndef STRING_VIEW_H
#define STRING_VIEW_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *start;
    size_t length;
} StringView;

bool cstr_starts_with_sv(const char *str, StringView with);
bool sv_equal(StringView a, StringView b);
bool sv_equal_cstr(StringView a, const char *b);

bool convenient_starts_with(const char *str, const char *with);
size_t convenient_strlen(const char *str);

#endif
