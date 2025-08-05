#ifndef STRING_VIEW_H
#define STRING_VIEW_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *start;
    size_t length;
} xml_StringView;

bool starts_with_str(const char *str, xml_StringView with);
bool xml_str_eq(xml_StringView a, xml_StringView b);
bool xml_str_eq_cstr(xml_StringView a, const char *b);

bool starts_with_cstr(const char *str, const char *with);
size_t xml_strlen(const char *str);

#endif
