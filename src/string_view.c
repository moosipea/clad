#include "string_view.h"

bool cstr_starts_with_sv(const char *str, StringView with) {
    for (size_t i = 0; i < with.length; i++) {
        if (str[i] == '\0' || str[i] != with.start[i]) {
            return false;
        }
    }

    return true;
}

bool sv_equal(StringView a, StringView b) {
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

bool sv_equal_cstr(StringView a, const char *b) {
    if (a.length != convenient_strlen(b)) {
        return false;
    }

    for (size_t i = 0; i < a.length; i++) {
        if (a.start[i] != b[i]) {
            return false;
        }
    }
    return true;
}

bool convenient_starts_with(const char *str, const char *with) {
    for (size_t i = 0; with[i] != '\0'; i++) {
        if (str[i] == '\0' || str[i] != with[i]) {
            return false;
        }
    }

    return true;
}

size_t convenient_strlen(const char *str) {
    size_t length = 0;
    while (str[length] != '\0') {
        length++;
    }
    return length;
}
