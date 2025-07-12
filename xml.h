#ifndef XML_H
#define XML_H

#include <stdio.h>
#include <stdbool.h>

typedef enum {
    XML_TOKEN_TEXT,
    XML_TOKEN_NODE
} XML_TokenType;

typedef struct {
    const char *start;
    size_t length;
} XML_StringView;

typedef struct {
    XML_StringView name;
    XML_StringView value;
} XML_Attrib;

typedef struct {
    XML_Attrib *attribs;
    size_t length;
    size_t capacity;
} XML_Attribs;

typedef struct {
    XML_StringView name;
    XML_Attribs attribs;
} XML_Tag;

typedef struct {
    XML_Tag tag;
    struct XML_Token *tokens;
    size_t length;
    size_t capacity;
} XML_ContentList;

typedef struct XML_Token {
    XML_TokenType type;
    union {
        XML_StringView text;
        XML_ContentList content;
    } value;
} XML_Token;

bool XML_parse_file(const char *src, XML_Token *token);
void XML_free(XML_Token root);

bool XML_get_attribute(XML_Token token, const char *property, XML_StringView *out);

char *XML_read_file(const char *file_name);
void XML_debug_print(FILE *file, XML_Token root);
bool XML_str_eq(XML_StringView a, XML_StringView b);
bool XML_str_eq_cstr(XML_StringView a, const char *b);
size_t XML_strlen(const char *str);

#endif
