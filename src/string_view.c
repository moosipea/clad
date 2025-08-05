#include "string_view.h"

bool XML_str_eq(XML_StringView a, XML_StringView b) {
    if (a.length != b.length) {
        return false;
    }

    for (size_t i = 0; i < a.length; i++) {
        if (a.start[i] != b.start[i]) {
            return false;
        }
    }
    return true;
}

bool XML_str_eq_cstr(XML_StringView a, const char *b) {
    if (a.length != XML_strlen(b)) {
        return false;
    }

    for (size_t i = 0; i < a.length; i++) {
        if (a.start[i] != b[i]) {
            return false;
        }
    }
    return true;
}

size_t XML_strlen(const char *str) {
    size_t length = 0;
    while (str[length] != '\0') {
        length++;
    }
    return length;
}
