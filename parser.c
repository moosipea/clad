#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>

#define DYNARRAY_START_CAP 8
#define DYNARRAY_GROWTH 2

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

typedef struct {
    const char *src;
    size_t cursor;
} XML_Context;

void XML_add_content(XML_ContentList *content_list, XML_Token content) 
{
    if (content_list->tokens == NULL) 
    {
        content_list->capacity = DYNARRAY_START_CAP;
        content_list->tokens = calloc(content_list->capacity, sizeof(content));
    }

    if (content_list->length >= content_list->capacity)
    {
        content_list->capacity *= DYNARRAY_GROWTH;
        content_list->tokens = realloc(content_list->tokens, content_list->capacity * sizeof(*content_list->tokens));
    }

    content_list->tokens[content_list->length++] = content;
}

void XML_add_attrib(XML_Tag *tag, XML_Attrib attrib) 
{
    XML_Attribs *attribs = &tag->attribs;
    if (attribs->attribs == NULL) 
    {
        attribs->capacity = DYNARRAY_START_CAP;
        attribs->attribs = calloc(attribs->capacity, sizeof(attrib));
    }

    if (attribs->length >= attribs->capacity) 
    {
        attribs->capacity *= DYNARRAY_GROWTH;
        attribs->attribs = realloc(attribs->attribs, attribs->capacity * sizeof(*attribs->attribs));
    }

    attribs->attribs[attribs->length++] = attrib;
}

XML_StringView XML_take_until_tag(XML_Context *ctx) 
{
    XML_StringView str = {0};

    str.start = &ctx->src[ctx->cursor];
    while (ctx->src[ctx->cursor] != '<') 
    {
        ctx->cursor++;
        str.length++;
    }

    return str;
}

void XML_skip_ws(XML_Context *ctx) 
{
    while (isspace(ctx->src[ctx->cursor])) 
    {
        ctx->cursor++; 
    }
}

XML_StringView XML_parse_ident(XML_Context *ctx) 
{
    XML_StringView str = {0};
    str.start = &ctx->src[ctx->cursor];

    while (isalpha(ctx->src[ctx->cursor]) || isdigit(ctx->src[ctx->cursor])) 
    {
        ctx->cursor++; 
        str.length++;
    }

    return str;
}

bool XML_expect(XML_Context *ctx, char ch) 
{
    if (ctx->src[ctx->cursor] == ch) 
    {
        ctx->cursor++;
        return true;
    }

    return false;
}

bool XML_expect_str(XML_Context *ctx, XML_StringView str) 
{
    for (size_t i = 0; i < str.length; i++) 
    {
        if (ctx->src[ctx->cursor + i] == '\0' || ctx->src[ctx->cursor + i] != str.start[i]) 
        {
            return false;
        }
    }

    ctx->cursor += str.length;
    return true;
}

size_t XML_strlen(const char *str) 
{
    size_t length = 0;
    while (str[length] != '\0') 
    {
        length++;
    }
    return length;
}

bool XML_expect_cstr(XML_Context *ctx, const char *str) 
{
    size_t length = XML_strlen(str);

    for (size_t i = 0; i < length; i++) 
    {
        if (ctx->src[ctx->cursor + i] == '\0' || ctx->src[ctx->cursor + i] != str[i]) 
        {
            return false;
        }
    }

    ctx->cursor += length;
    return true;
}

XML_StringView XML_parse_string_literal(XML_Context *ctx) 
{
    XML_expect(ctx, '"');

    XML_StringView str = {0};
    str.start = &ctx->src[ctx->cursor];

    while (ctx->src[ctx->cursor] != '"') 
    {
        if (ctx->src[ctx->cursor] == '\0') 
        {
            fprintf(stderr, "XML error: unexpected EOF!\n");
            break;
        }

        ctx->cursor++;
        str.length++;
    }

    XML_expect(ctx, '"');
    return str;
}

XML_Attrib XML_parse_attrib(XML_Context *ctx) 
{
    XML_Attrib attrib = {0};

    attrib.name = XML_parse_ident(ctx);
    XML_expect(ctx, '=');
    attrib.value = XML_parse_string_literal(ctx);

    return attrib;
}

bool XML_attempt_parse_end_tag(XML_Context *ctx, XML_StringView tag) 
{
    size_t previous_cursor = ctx->cursor;

    if (XML_expect_cstr(ctx, "</") && XML_expect_str(ctx, tag) && XML_expect(ctx, '>')) 
    {
        return true;
    }

    ctx->cursor = previous_cursor;
    return false;
}

XML_Token XML_parse(XML_Context *ctx);

XML_ContentList XML_parse_content(XML_Context *ctx) 
{
    XML_ContentList content_list = {0};

    XML_expect(ctx, '<');
    XML_skip_ws(ctx);
    content_list.tag.name = XML_parse_ident(ctx);
    XML_skip_ws(ctx);

    while (ctx->src[ctx->cursor] != '>') 
    {
        XML_Attrib attrib = XML_parse_attrib(ctx);
        XML_add_attrib(&content_list.tag, attrib);
        XML_skip_ws(ctx);
    }

    XML_expect(ctx, '>');

    while (!XML_attempt_parse_end_tag(ctx, content_list.tag.name)) 
    {
        XML_Token next_token = XML_parse(ctx);
        XML_add_content(&content_list, next_token);
    }

    return content_list;
}

// TODO: Use this in expect functions
bool XML_starts_with(const char *str, const char *with)
{
    for (size_t i = 0; with[i] != '\0'; i++)
    {
        if (str[i] == '\0' || str[i] != with[i]) 
        {
            return false;
        } 
    }

    return true;
}

void XML_skip_comment(XML_Context *ctx)
{
    XML_expect_cstr(ctx, "<!--");
    
    while (ctx->src[ctx->cursor] != '\0' && !XML_starts_with(&ctx->src[ctx->cursor], "-->")) 
    {
        ctx->cursor++;
    }

    // Account for "-->"
    ctx->cursor += 3;
}

XML_Token XML_parse(XML_Context *ctx)
{
    XML_Token token = {0};

    if (ctx->src[ctx->cursor] == '<')
    {
        if (ctx->src[ctx->cursor + 1] == '!')
        {
            XML_skip_comment(ctx);
            XML_skip_ws(ctx);
            return XML_parse(ctx);
        }

        token.type = XML_TOKEN_NODE; 
        token.value.content = XML_parse_content(ctx);
    } 
    else 
    {
        token.type = XML_TOKEN_TEXT;
        token.value.text = XML_take_until_tag(ctx);
    }

    return token;
}

void XML_free_recursively(XML_ContentList content) 
{
    for (size_t i = 0; i < content.length; i++) 
    {
        if (content.tokens[i].type == XML_TOKEN_NODE) 
        {
            XML_free_recursively(content.tokens[i].value.content);
        }
    }
    free(content.tokens);
    free(content.tag.attribs.attribs);
}

void XML_free(XML_Token root) 
{
    if (root.type == XML_TOKEN_TEXT) 
    {
        return;
    }
    XML_free_recursively(root.value.content);
}

int main(int argc, char **argv) 
{
    if (argc < 2) 
    {
        fprintf(stderr, "Usage: %s <xml file>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "r");
    if (!fp) 
    {
        fprintf(stderr, "Error opening file `%s`!\n", argv[1]);
        return 1;
    }

    long length;
    fseek(fp, 0, SEEK_END);
    length = ftell(fp);
    rewind(fp);

    char *input_buffer = malloc(length + 1);
    fread(input_buffer, 1, length, fp);
    input_buffer[length] = '\0';
    fclose(fp);

    XML_Context parse_ctx = {
        .src = input_buffer,
        .cursor = 0,
    };

    XML_Token root = XML_parse(&parse_ctx);
    XML_free(root);
    free(input_buffer);

    return 0;
}
