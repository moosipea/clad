#ifndef TEMPLATE_H
#define TEMPLATE_H

#include "string_buffer.h"
#include "string_view.h"

typedef struct {
    StringBuffer str;
} Template;

Template template_init(const char *template);
void template_free(Template *template);
void template_replace_sv(Template *template, const char *key, StringView sv);

#endif
