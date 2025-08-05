#include "string_view.h"

bool starts_with_str(const char *str, xml_StringView with) {
    for (size_t i = 0; i < with.length; i++) {
        if (str[i] == '\0' || str[i] != with.start[i]) {
            return false;
        }
    }

    return true;
}

bool xml_str_eq(xml_StringView a, xml_StringView b) {
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

bool xml_str_eq_cstr(xml_StringView a, const char *b) {
    if (a.length != xml_strlen(b)) {
        return false;
    }

    for (size_t i = 0; i < a.length; i++) {
        if (a.start[i] != b[i]) {
            return false;
        }
    }
    return true;
}

bool starts_with_cstr(const char *str, const char *with) {
    for (size_t i = 0; with[i] != '\0'; i++) {
        if (str[i] == '\0' || str[i] != with[i]) {
            return false;
        }
    }

    return true;
}

size_t xml_strlen(const char *str) {
    size_t length = 0;
    while (str[length] != '\0') {
        length++;
    }
    return length;
}

