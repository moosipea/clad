#ifndef STRING_VIEW_H
#define STRING_VIEW_H

#include <stdbool.h>

typedef struct {
    const char *start;
    size_t length;
} XML_StringView;

bool XML_str_eq(XML_StringView a, XML_StringView b);
bool XML_str_eq_cstr(XML_StringView a, const char *b);
size_t XML_strlen(const char *str);

#endif
