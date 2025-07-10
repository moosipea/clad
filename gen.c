#include "xml.h"
#include <stdlib.h>

#define PREFIX "clad_"

XML_Token *find_next(XML_Token parent, const char *tag, size_t *index)
{
    size_t local_index = 0;

    if (index == NULL) {
        index = &local_index;
    }

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
            break;
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
        XML_Token token = types->value.content.tokens[i];
        if (token.type == XML_TOKEN_TEXT || !XML_str_eq_cstr(token.value.content.tag.name, "type")) 
        {
            continue; 
        }
        write_inner_text(file, token);
        fputc('\n', file);
    }
}

void generate_command(FILE *file, XML_Token command) 
{
    size_t tag_index = 0;
    XML_Token *proto = find_next(command, "proto", &tag_index);

    if (proto->type != XML_TOKEN_NODE || proto->value.content.length != 2) 
    {
        fprintf(stderr, "Generation error: invalid command tag!\n");
        return;
    }

    XML_Token return_type = proto->value.content.tokens[0];
    XML_Token command_name = proto->value.content.tokens[1];

    write_inner_text(file, return_type);
    fputs(PREFIX, file);
    write_inner_text(file, command_name);

    // Function parameters
    fputc('(', file);

    XML_Token *next_param = NULL;
    bool first_param = true; 

    while ((next_param = find_next(command, "param", &tag_index))) 
    {
        if (!first_param) 
        {
            fputs(", ", file);
        } 
        else 
        {
            first_param = false;
        }

        write_inner_text(file, *next_param);
    }

    fputs(");", file);
}

void generate_commands(FILE *file, XML_Token *commands) 
{
    for (size_t i = 0; i < commands->value.content.length; i++) 
    {
        XML_Token token = commands->value.content.tokens[i];
        if (token.type == XML_TOKEN_TEXT || !XML_str_eq_cstr(token.value.content.tag.name, "command")) 
        {
            continue;
        }
        generate_command(file, token);
        fputs("\n\n", file);
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
    XML_Token *commands_section = find_next(root, "commands", &section_index);

    generate_types(file, types_section);
    generate_commands(file, commands_section);
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
