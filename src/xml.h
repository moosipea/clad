#ifndef XML_H
#define XML_H

#include "string_view.h"
#include <stdbool.h>
#include <stdio.h>

typedef enum { XML_TOKEN_TEXT, XML_TOKEN_NODE } xml_TokenType;

typedef struct {
    StringView name;
    StringView value;
} xml_Attrib;

typedef struct {
    xml_Attrib *attribs;
    size_t length;
    size_t capacity;
} xml_Attribs;

typedef struct {
    StringView name;
    xml_Attribs attribs;
} xml_Tag;

typedef struct {
    xml_Tag tag;
    struct _xml_Token *tokens;
    size_t length;
    size_t capacity;
} xml_ContentList;

typedef struct _xml_Token {
    xml_TokenType type;
    union {
        StringView text;
        xml_ContentList content;
    } value;
} xml_Token;

bool xml_parse_file(const char *src, xml_Token *token);
void xml_free(xml_Token root);

bool xml_get_attribute(xml_Token token, const char *property,
                       StringView *out);

char *xml_read_file(const char *file_name);
void xml_debug_print(FILE *file, xml_Token root);

#endif
