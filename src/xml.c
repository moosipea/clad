#include "xml.h"
#include <assert.h>
#include <ctype.h>
#include <setjmp.h>
#include <stdlib.h>

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#define DYNARRAY_START_CAP 8
#define DYNARRAY_GROWTH 2

typedef struct {
    const char *src;
    size_t cursor;
    jmp_buf on_error;
} ParseContext;

static void add_content(xml_ContentList *content_list, xml_Token content) {
    if (content_list->tokens == NULL) {
        content_list->capacity = DYNARRAY_START_CAP;
        content_list->tokens = calloc(content_list->capacity, sizeof(content));
    }

    if (content_list->length >= content_list->capacity) {
        content_list->capacity *= DYNARRAY_GROWTH;
        content_list->tokens =
            realloc(content_list->tokens,
                    content_list->capacity * sizeof(*content_list->tokens));
    }

    content_list->tokens[content_list->length++] = content;
}

static void add_attrib(xml_Tag *tag, xml_Attrib attrib) {
    xml_Attribs *attribs = &tag->attribs;
    if (attribs->attribs == NULL) {
        attribs->capacity = DYNARRAY_START_CAP;
        attribs->attribs = calloc(attribs->capacity, sizeof(attrib));
    }

    if (attribs->length >= attribs->capacity) {
        attribs->capacity *= DYNARRAY_GROWTH;
        attribs->attribs = realloc(
            attribs->attribs, attribs->capacity * sizeof(*attribs->attribs));
    }

    attribs->attribs[attribs->length++] = attrib;
}

static xml_StringView take_until_tag(ParseContext *ctx) {
    xml_StringView str = { 0 };

    str.start = &ctx->src[ctx->cursor];
    while (ctx->src[ctx->cursor] != '<') {
        if (ctx->src[ctx->cursor] == '\0') {
            fprintf(
                stderr,
                "XML error: unexpected EOF while parsing text! (cursor=%d)\n",
                (int)ctx->cursor);
            break;
        }
        ctx->cursor++;
        str.length++;
    }

    return str;
}

static void skip_whitespace(ParseContext *ctx) {
    while (isspace(ctx->src[ctx->cursor])) {
        ctx->cursor++;
    }
}

static xml_StringView parse_ident(ParseContext *ctx) {
    xml_StringView str = { 0 };
    str.start = &ctx->src[ctx->cursor];

    while (isalpha(ctx->src[ctx->cursor]) || isdigit(ctx->src[ctx->cursor])) {
        ctx->cursor++;
        str.length++;
    }

    return str;
}

static bool expect(ParseContext *ctx, char ch) {
    if (ctx->src[ctx->cursor] == ch) {
        ctx->cursor++;
        return true;
    }

    fprintf(stderr, "XML error: expected '%c'!\n", ch);
    longjmp(ctx->on_error, 1);
    return false;
}

static bool expect_cstr(ParseContext *ctx, const char *str) {
    size_t length = xml_strlen(str);
    if (starts_with_cstr(&ctx->src[ctx->cursor], str)) {
        ctx->cursor += length;
        return true;
    }

    fprintf(stderr, "XML error: expected \"%s\"\n", str);
    longjmp(ctx->on_error, 1);
    return false;
}

static xml_StringView parse_string_literal(ParseContext *ctx) {
    expect(ctx, '"');

    xml_StringView str = { 0 };
    str.start = &ctx->src[ctx->cursor];

    while (ctx->src[ctx->cursor] != '"') {
        if (ctx->src[ctx->cursor] == '\0') {
            fprintf(stderr, "XML error: unexpected EOF!\n");
            break;
        }

        ctx->cursor++;
        str.length++;
    }

    expect(ctx, '"');
    return str;
}

static xml_Attrib parse_attrib(ParseContext *ctx) {
    xml_Attrib attrib = { 0 };

    attrib.name = parse_ident(ctx);
    expect(ctx, '=');
    attrib.value = parse_string_literal(ctx);

    return attrib;
}

static bool attempt_parse_end_tag(ParseContext *ctx, xml_StringView tag) {
    size_t cursor = ctx->cursor;
    bool matches = true;

    matches &= starts_with_cstr(&ctx->src[cursor], "</");
    cursor += 2;
    if (!matches) {
        return false;
    }

    matches &= starts_with_str(&ctx->src[cursor], tag);
    cursor += tag.length;
    if (!matches) {
        return false;
    }

    matches &= starts_with_cstr(&ctx->src[cursor], ">");
    cursor++;
    if (!matches) {
        return false;
    }

    ctx->cursor = cursor;
    return true;
}

static xml_Token parse_xml(ParseContext *ctx);

static xml_ContentList parse_content(ParseContext *ctx) {
    xml_ContentList content_list = { 0 };

    expect(ctx, '<');
    skip_whitespace(ctx);
    content_list.tag.name = parse_ident(ctx);
    skip_whitespace(ctx);

    while (ctx->src[ctx->cursor] != '>' && ctx->src[ctx->cursor] != '/') {
        xml_Attrib attrib = parse_attrib(ctx);
        add_attrib(&content_list.tag, attrib);
        skip_whitespace(ctx);
    }

    // Self-closing tag
    if (ctx->src[ctx->cursor] == '/') {
        expect_cstr(ctx, "/>");
    } else {
        expect(ctx, '>');
        while (!attempt_parse_end_tag(ctx, content_list.tag.name)) {
            if (ctx->src[ctx->cursor] == '\0') {
                fprintf(stderr,
                        "XML error: expected closing tag, but got EOF!\n");
                break;
            }
            xml_Token next_token = parse_xml(ctx);
            add_content(&content_list, next_token);
        }
    }

    return content_list;
}

static void skip_comment(ParseContext *ctx) {
    expect_cstr(ctx, "<!--");

    while (ctx->src[ctx->cursor] != '\0' &&
           !starts_with_cstr(&ctx->src[ctx->cursor], "-->")) {
        ctx->cursor++;
    }

    if (ctx->src[ctx->cursor] == '\0') {
        fprintf(stderr, "XML error: unterminated comment!\n");
    } else {
        // Account for "-->"
        ctx->cursor += 3;
    }
}

static xml_Token parse_xml(ParseContext *ctx) {
    xml_Token token = { 0 };

    if (starts_with_cstr(&ctx->src[ctx->cursor], "<!--")) {
        skip_comment(ctx);
    }

    if (ctx->src[ctx->cursor] == '<') {
        token.type = XML_TOKEN_NODE;
        token.value.content = parse_content(ctx);
    } else if (ctx->src[ctx->cursor] != '\0') {
        token.type = XML_TOKEN_TEXT;
        token.value.text = take_until_tag(ctx);
    }

    return token;
}

static void free_recursively(xml_ContentList content) {
    for (size_t i = 0; i < content.length; i++) {
        if (content.tokens[i].type == XML_TOKEN_NODE) {
            free_recursively(content.tokens[i].value.content);
        }
    }
    free(content.tokens);
    free(content.tag.attribs.attribs);
}

void xml_free(xml_Token root) {
    if (root.type == XML_TOKEN_TEXT) {
        return;
    }
    free_recursively(root.value.content);
}

void xml_debug_print(FILE *file, xml_Token root) {
    if (root.type == XML_TOKEN_TEXT) {
        fprintf(file, "%.*s", (int)root.value.text.length,
                root.value.text.start);
    } else {
        xml_ContentList content = root.value.content;
        xml_StringView tag_name = content.tag.name;
        xml_Attribs attribs = content.tag.attribs;

        fprintf(file, "<%.*s", (int)tag_name.length, tag_name.start);
        for (size_t i = 0; i < attribs.length; i++) {
            xml_Attrib attrib = attribs.attribs[i];
            fprintf(file, " %.*s=\"%.*s\"", (int)attrib.name.length,
                    attrib.name.start, (int)attrib.value.length,
                    attrib.value.start);
        }
        fputc('>', file);

        for (size_t j = 0; j < content.length; j++) {
            xml_debug_print(file, content.tokens[j]);
        }

        fprintf(file, "</%.*s>", (int)tag_name.length, tag_name.start);
    }
}

char *xml_read_file(const char *file_name) {
    FILE *fp = fopen(file_name, "r");
    if (!fp) {
        fprintf(stderr, "Error opening file `%s`!\n", file_name);
        return NULL;
    }

    size_t length;
    fseek(fp, 0, SEEK_END);
    length = ftell(fp);
    rewind(fp);

    char *input_buffer = malloc(length + 1);
    size_t elements_read = fread(input_buffer, 1, length, fp);

    // TODO: this should really be checked but I don't have time for that now.
    (void)elements_read;

    input_buffer[length] = '\0';
    fclose(fp);

    return input_buffer;
}

bool xml_parse_file(const char *src, xml_Token *token) {
    ParseContext ctx = {
        .src = src,
        .cursor = 0,
    };

    if (!expect_cstr(&ctx, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>")) {
        fprintf(stderr, "XML error: failed to parse header!\n");
        return false;
    }

    if (setjmp(ctx.on_error)) {
        fprintf(stderr, "XML error: parsing failed!\n");
        return false;
    } else {
        skip_whitespace(&ctx);
        *token = parse_xml(&ctx);
        return true;
    }
}

bool xml_get_attribute(xml_Token token, const char *property,
                       xml_StringView *out) {
    if (token.type != XML_TOKEN_NODE) {
        fprintf(stderr, "XML_get_attribute: expected a node, got text!\n");
        return false;
    }
    for (size_t i = 0; i < token.value.content.tag.attribs.length; i++) {
        xml_Attrib attrib = token.value.content.tag.attribs.attribs[i];
        if (xml_str_eq_cstr(attrib.name, property)) {
            *out = attrib.value;
            return true;
        }
    }

    return false;
}
