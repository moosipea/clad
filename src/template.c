#include "template.h"
#include "string_buffer.h"
#include "string_view.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#define VARIABLE_START_COUNT 8

void template_define(Template *t, const char *name, StringView value) {
    if (t->variables == NULL) {
        t->variable_capacity = VARIABLE_START_COUNT;
        t->variables = calloc(t->variable_capacity, sizeof(*t->variables));
    }

    if (t->variable_count >= t->variable_capacity) {
        t->variable_capacity *= 2;
        t->variables =
            realloc(t->variables, t->variable_capacity * sizeof(*t->variables));
    }

    t->variables[t->variable_count++] = (TemplateVariable){
        .name = name,
        .value = value,
    };
}

StringBuffer template_build(Template *template, const char *source) {
    StringBuffer sb = sb_new_buffer();

    char ch;
    while ((ch = (source++)[0]) != '\0') {
        if (ch != '%') {
            sb_putc(ch, &sb);
            continue;
        }

        bool found = false;
        for (size_t i = 0; i < template->variable_count; i++) {
            TemplateVariable var = template->variables[i];
            size_t var_name_len = convenient_strlen(var.name);

            if (!convenient_starts_with(source, var.name)) {
                continue;
            }

            if (source[var_name_len] != '%') {
                continue;
            }

            source = &source[var_name_len + 1];
            sb_putsn(&sb, var.value.start, var.value.length);
            found = true;
            break;
        }

        if (!found) {
            fprintf(stderr, "error: unknown template variable\n");
        }
    }

    return sb;
}

void template_free(Template *template) { free(template->variables); }
