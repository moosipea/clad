#include "xml.h"
#include <stdlib.h>
#include <ctype.h>
#include <setjmp.h>

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#define DYNARRAY_START_CAP 8
#define DYNARRAY_GROWTH 2

typedef struct {
    const char *src;
    size_t cursor;
    jmp_buf on_error;
} XML_Context;

static void XML_add_content(XML_ContentList *content_list, XML_Token content) 
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

static void XML_add_attrib(XML_Tag *tag, XML_Attrib attrib) 
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

static XML_StringView XML_take_until_tag(XML_Context *ctx) 
{
    XML_StringView str = {0};

    str.start = &ctx->src[ctx->cursor];
    while (ctx->src[ctx->cursor] != '<') 
    {
        if (ctx->src[ctx->cursor] == '\0')
        {
            fprintf(stderr, "XML error: unexpected EOF while parsing text! (cursor=%d)\n", (int)ctx->cursor);
            break;
        }
        ctx->cursor++;
        str.length++;
    }

    return str;
}

static void XML_skip_ws(XML_Context *ctx) 
{
    while (isspace(ctx->src[ctx->cursor])) 
    {
        ctx->cursor++; 
    }
}

static XML_StringView XML_parse_ident(XML_Context *ctx) 
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

static bool XML_starts_with_str(const char *str, XML_StringView with) 
{
    for (size_t i = 0; i < with.length; i++) 
    {
        if (str[i] == '\0' || str[i] != with.start[i]) 
        {
            return false;
        }
    }
    
    return true;
}

static bool XML_starts_with_cstr(const char *str, const char *with)
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

static size_t XML_strlen(const char *str) 
{
    size_t length = 0;
    while (str[length] != '\0') 
    {
        length++;
    }
    return length;
}

static bool XML_expect(XML_Context *ctx, char ch) 
{
    if (ctx->src[ctx->cursor] == ch) 
    {
        ctx->cursor++;
        return true;
    }

    fprintf(stderr, "XML error: expected '%c'!\n", ch);
    longjmp(ctx->on_error, 1);
    return false;
}

static bool XML_expect_cstr(XML_Context *ctx, const char *str) 
{
    size_t length = XML_strlen(str);
    if (XML_starts_with_cstr(&ctx->src[ctx->cursor], str)) 
    {
        ctx->cursor += length;
        return true;
    }

    fprintf(stderr, "XML error: expected \"%s\"\n", str);
    longjmp(ctx->on_error, 1);
    return false;
}

static XML_StringView XML_parse_string_literal(XML_Context *ctx) 
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

static XML_Attrib XML_parse_attrib(XML_Context *ctx) 
{
    XML_Attrib attrib = {0};

    attrib.name = XML_parse_ident(ctx);
    XML_expect(ctx, '=');
    attrib.value = XML_parse_string_literal(ctx);

    return attrib;
}

static bool XML_attempt_parse_end_tag(XML_Context *ctx, XML_StringView tag) 
{
    size_t cursor = ctx->cursor;
    bool matches = true;

    matches &= XML_starts_with_cstr(&ctx->src[cursor], "</");
    cursor += 2;
    if (!matches) {
        return false;
    }

    matches &= XML_starts_with_str(&ctx->src[cursor], tag);
    cursor += tag.length;
    if (!matches) {
        return false;
    }

    matches &= XML_starts_with_cstr(&ctx->src[cursor], ">");
    cursor++;
    if (!matches) {
        return false;
    }

    ctx->cursor = cursor;
    return true;
}

static XML_Token XML_parse(XML_Context *ctx);

static XML_ContentList XML_parse_content(XML_Context *ctx) 
{
    XML_ContentList content_list = {0};

    XML_expect(ctx, '<');
    XML_skip_ws(ctx);
    content_list.tag.name = XML_parse_ident(ctx);
    XML_skip_ws(ctx);

    while (ctx->src[ctx->cursor] != '>' && ctx->src[ctx->cursor] != '/') 
    {
        XML_Attrib attrib = XML_parse_attrib(ctx);
        XML_add_attrib(&content_list.tag, attrib);
        XML_skip_ws(ctx);
    }

    // Self-closing tag
    if (ctx->src[ctx->cursor] == '/') 
    {
        XML_expect_cstr(ctx, "/>");
    } 
    else 
    {
        XML_expect(ctx, '>');
        while (!XML_attempt_parse_end_tag(ctx, content_list.tag.name)) 
        {
            if (ctx->src[ctx->cursor] == '\0') 
            {
                fprintf(stderr, "XML error: expected closing tag, but got EOF!\n");
                break;
            }
            XML_Token next_token = XML_parse(ctx);
            XML_add_content(&content_list, next_token);
        }
    }

    return content_list;
}

static void XML_skip_comment(XML_Context *ctx)
{
    XML_expect_cstr(ctx, "<!--");
    
    while (ctx->src[ctx->cursor] != '\0' && !XML_starts_with_cstr(&ctx->src[ctx->cursor], "-->")) 
    {
        ctx->cursor++;
    }

    if (ctx->src[ctx->cursor] == '\0') 
    {
        fprintf(stderr, "XML error: unterminated comment!\n");
    }
    else 
    {
        // Account for "-->"
        ctx->cursor += 3;
    }
}

static XML_Token XML_parse(XML_Context *ctx)
{
    XML_Token token = {0};

    if (XML_starts_with_cstr(&ctx->src[ctx->cursor], "<!--")) {
        XML_skip_comment(ctx);
    }

    if (ctx->src[ctx->cursor] == '<')
    {
        token.type = XML_TOKEN_NODE; 
        token.value.content = XML_parse_content(ctx);
    } 
    else if (ctx->src[ctx->cursor] != '\0')
    {
        token.type = XML_TOKEN_TEXT;
        token.value.text = XML_take_until_tag(ctx);
    }

    return token;
}

static void XML_free_recursively(XML_ContentList content) 
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

void XML_debug_print(FILE *file, XML_Token root) 
{
    if (root.type == XML_TOKEN_TEXT) 
    {
        fprintf(file, "%.*s", (int)root.value.text.length, root.value.text.start);
    }
    else 
    {
        XML_ContentList content = root.value.content;
        XML_StringView tag_name = content.tag.name;
        XML_Attribs attribs = content.tag.attribs;

        fprintf(file, "<%.*s", (int)tag_name.length, tag_name.start);
        for (size_t i = 0; i < attribs.length; i++) 
        {
            XML_Attrib attrib = attribs.attribs[i];
            fprintf(file, " %.*s=\"%.*s\"", 
                    (int)attrib.name.length, attrib.name.start, 
                    (int)attrib.value.length, attrib.value.start);
        }
        fputc('>', file);

        for (size_t j = 0; j < content.length; j++) {
            XML_debug_print(file, content.tokens[j]);
        }

        fprintf(file, "</%.*s>", (int)tag_name.length, tag_name.start);
    }
}

char *XML_read_file(const char *file_name) 
{
    FILE *fp = fopen(file_name, "r");
    if (!fp) 
    {
        fprintf(stderr, "Error opening file `%s`!\n", file_name);
        return NULL;
    }

    long length;
    fseek(fp, 0, SEEK_END);
    length = ftell(fp);
    rewind(fp);

    char *input_buffer = malloc(length + 1);
    fread(input_buffer, 1, length, fp);
    input_buffer[length] = '\0';
    fclose(fp);

    return input_buffer;
}

bool XML_parse_file(const char *src, XML_Token *token)
{
    XML_Context ctx = {
        .src = src,
        .cursor = 0,
    };

    if (!XML_expect_cstr(&ctx, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>")) 
    {
        fprintf(stderr, "XML error: failed to parse header!\n");
        return false;
    }

    if (setjmp(ctx.on_error)) 
    {
        fprintf(stderr, "XML error: parsing failed!\n");
        return false;
    }
    else 
    {
        XML_skip_ws(&ctx);
        *token = XML_parse(&ctx);
        return true;
    }
}

bool XML_str_eq(XML_StringView a, XML_StringView b) 
{
    if (a.length != b.length)
    {
        return false; 
    }

    for (size_t i = 0; i < a.length; i++) 
    {
        if (a.start[i] != b.start[i]) 
        {
            return false;
        }
    }
    return true;
}

bool XML_str_eq_cstr(XML_StringView a, const char *b) 
{
    if (a.length != XML_strlen(b))
    {
        return false; 
    }

    for (size_t i = 0; i < a.length; i++) 
    {
        if (a.start[i] != b[i]) 
        {
            return false;
        }
    }
    return true;
}
