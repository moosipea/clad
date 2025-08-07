#ifndef TEMPLATE_H
#define TEMPLATE_H

#include "string_buffer.h"
#include "string_view.h"
#include <stddef.h>

typedef struct {
    const char *name;
    StringView value;
} TemplateVariable;

typedef struct {
    TemplateVariable *variables;
    size_t variable_count;
    size_t variable_capacity;
} Template;

void template_define(Template *template, const char *name, StringView value);
StringBuffer template_build(Template *template, const char *source);
void template_free(Template *template);

#endif
