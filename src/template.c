#include "template.h"
#include "string_buffer.h"
#include "string_view.h"
#include <stddef.h>

static int find_key(StringBuffer str, const char *key) {
    for (size_t i = 0; i < str.length; i++) {
        StringView substr = {
            .start = &str.ptr[i],
            .length = substr.length - i,
        };
        if (sv_equal_cstr(substr, key)) {
            return i;
        }
    }
    return -1;
}

Template template_init(const char *template) {
    Template templ = { 0 };
    templ.str = sb_new_buffer();
    sb_puts(template, &templ.str);
    return templ;
}

void template_free(Template *template) { sb_free(template->str); }

void template_replace_sv(Template *template, const char *key, StringView sv) {
    int position = find_key(template->str, key);
    if (position < 0) {
        return;
    }

    StringBuffer sb = sb_new_buffer();

    if (position == 0) {
        sb_putsn(&sb, sv.start, sv.length);
        sb_putsn(&sb, template->str.ptr, template->str.length);
    } else {
        size_t key_length = convenient_strlen(key);

        StringView pre = {
            .start = template->str.ptr,
            .length = position,
        };
        StringView post = {
            .start = &template->str.ptr[position + key_length],
            .length = template->str.length - position - key_length,
        };

        sb_putsn(&sb, pre.start, pre.length);
        sb_putsn(&sb, sv.start, sv.length);
        sb_putsn(&sb, post.start, post.length);
    }

    sb_free(template->str);
    template->str = sb;
}
