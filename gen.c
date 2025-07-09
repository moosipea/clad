#include "xml.h"
#include <stdlib.h>

XML_Token *find_next(XML_Token parent, const char *tag, size_t *index)
{
    while (*index < parent.value.content.length) 
    {
        XML_Token *child = &parent.value.content.tokens[(*index)++];
        if (XML_str_eq_cstr(child->value.content.tag.name, tag))
        {
            return child;
        }
    }
    return NULL;
}

bool sv_starts_with(XML_StringView str, const char *with) 
{
    for (size_t i = 0; i < str.length; i++) {
        if (with[i] == '\0') 
        {
            return false;
        }
        if (str.start[i] != with[i])
        {
            return false; 
        }
    } 

    return true;
}

void put_xml_string_view(FILE *file, XML_StringView str) 
{
    size_t i = 0;
    while (i < str.length) 
    {
        if (str.start[i] == '&') 
        {
            XML_StringView substr = {
                .start = &str.start[i],
                .length = str.length - i
            };

            if (sv_starts_with(substr, "&quot;")) 
            {
                fputc('"', file); 
                i += 6;
                continue;
            }
            else if (sv_starts_with(substr, "&apos;")) 
            {
                fputc('\'', file); 
                i += 6;
                continue;
            }
            else if (sv_starts_with(substr, "&lt;"))
            {
                fputc('<', file);
                i += 4;
                continue;
            }
            else if (sv_starts_with(substr, "&gt;")) 
            {
                fputc('>', file);
                i += 4;
                continue;
            }
            else if (sv_starts_with(substr, "&amp;"))
            {
                fputc('&', file);
                i += 5;
                continue;
            }
        }

        fputc(str.start[i], file);
        i++;
    }
}

void write_inner_text(FILE *file, XML_Token token)
{
    switch (token.type) 
    {
        case XML_TOKEN_TEXT:
            put_xml_string_view(file, token.value.text);
            break;
        case XML_TOKEN_NODE:
            for (size_t i = 0; i < token.value.content.length; i++) 
            {
                write_inner_text(file, token.value.content.tokens[i]);         
            }
            break;
    }
}

void generate_types(FILE *file, XML_Token *types)
{
    for (size_t i = 0; i < types->value.content.length; i++) 
    {
        write_inner_text(file, types->value.content.tokens[i]);
        fputc('\n', file);
    }
}

void generate(FILE *file, XML_Token root) 
{
    if (root.type != XML_TOKEN_NODE) 
    {
        fprintf(stderr, "Generator error: expected node, got text!\n");
        return;
    }

    size_t section_index = 0;
    XML_Token *types_section = find_next(root, "types", &section_index);
    generate_types(file, types_section);
}

int main(int argc, char **argv) 
{
    if (argc < 2) 
    {
        fprintf(stderr, "Usage: %s <xml file>\n", argv[0]);
        return 1;
    }

    XML_Token root;
    char *src = XML_read_file(argv[1]);
    if (!src) 
    {
        return 1;
    }

    if (XML_parse_file(src, &root)) 
    {
        generate(stdout, root);
        XML_free(root);
    }

    free(src);
    return 0;
}
