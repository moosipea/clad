#include "xml.h"
#include "string_buffer.h"
#include <stdlib.h>

#define PREFIX "clad_"

typedef struct {
    StringBuffer types;
    StringBuffer enums;
    StringBuffer command_lookup;
    StringBuffer commands;
    StringBuffer command_defines;
    size_t command_index;
} GenerationContext;

GenerationContext init_context(void) 
{
    GenerationContext ctx = {0};
    ctx.types = sb_new_buffer();
    ctx.enums = sb_new_buffer();
    ctx.command_lookup = sb_new_buffer();
    ctx.commands = sb_new_buffer();
    ctx.command_defines = sb_new_buffer();
    return ctx;
}

void free_context(GenerationContext ctx)
{
    sb_free(ctx.types);
    sb_free(ctx.enums);
    sb_free(ctx.command_lookup);
    sb_free(ctx.commands);
    sb_free(ctx.command_defines);
}

XML_Token *find_next(XML_Token parent, const char *tag, size_t *index)
{
    size_t local_index = 0;

    if (index == NULL) 
    {
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
    for (size_t i = 0; i < str.length; i++) 
    {
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

void put_xml_string_view(StringBuffer *file, XML_StringView str) 
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
                sb_putc('"', file); 
                i += 6;
                continue;
            }
            else if (sv_starts_with(substr, "&apos;")) 
            {
                sb_putc('\'', file); 
                i += 6;
                continue;
            }
            else if (sv_starts_with(substr, "&lt;"))
            {
                sb_putc('<', file);
                i += 4;
                continue;
            }
            else if (sv_starts_with(substr, "&gt;")) 
            {
                sb_putc('>', file);
                i += 4;
                continue;
            }
            else if (sv_starts_with(substr, "&amp;"))
            {
                sb_putc('&', file);
                i += 5;
                continue;
            }
        }

        sb_putc(str.start[i], file);
        i++;
    }
}

void write_inner_text(StringBuffer *buffer, XML_Token token, int count)
{
    switch (token.type) 
    {
        case XML_TOKEN_TEXT:
            put_xml_string_view(buffer, token.value.text);
            break;
        case XML_TOKEN_NODE:
            {
                size_t max = (count < 0) ? token.value.content.length : (size_t)count;
                for (size_t i = 0; i < max; i++) 
                {
                    write_inner_text(buffer, token.value.content.tokens[i], -1);         
                }
                break;
            }
    }
}

void generate_types(GenerationContext *ctx, XML_Token *types)
{
    for (size_t i = 0; i < types->value.content.length; i++) 
    {
        XML_Token token = types->value.content.tokens[i];
        if (token.type == XML_TOKEN_TEXT || !XML_str_eq_cstr(token.value.content.tag.name, "type")) 
        {
            continue; 
        }
        write_inner_text(&ctx->types, token, -1);
        sb_putc('\n', &ctx->types);
    }
}

void write_prototype(StringBuffer *sb, XML_Token command) 
{
    size_t tag_index = 0;
    // TODO: assert proto != NULL
    XML_Token *proto = find_next(command, "proto", &tag_index);
    XML_Token return_type = proto->value.content.tokens[0];
    XML_Token *command_name = find_next(*proto, "name", NULL);

    write_inner_text(sb, return_type, -1);

    // If the return type is `void`, we want to avoid adding an extra space but
    // otherwise it's required.
    if (return_type.type != XML_TOKEN_TEXT) 
    {
        sb_putc(' ', sb);
    }
    sb_puts(PREFIX, sb);
    write_inner_text(sb, *command_name, -1);

    // Function parameters
    sb_putc('(', sb);

    XML_Token *next_param = NULL;
    bool first_param = true; 

    while ((next_param = find_next(command, "param", &tag_index))) 
    {
        if (!first_param) 
        {
            sb_puts(", ", sb);
        } 
        else 
        {
            first_param = false;
        }

        write_inner_text(sb, *next_param, -1);
    }
    
    sb_puts(")", sb);
}

void write_as_function_ptr_type(StringBuffer *sb, XML_Token command) 
{
    size_t tag_index = 0;
    XML_Token *proto = find_next(command, "proto", &tag_index);
    XML_Token return_type = proto->value.content.tokens[0];

    write_inner_text(sb, return_type, -1);

    // If the return type is `void`, we want to avoid adding an extra space but
    // otherwise it's required.
    if (return_type.type != XML_TOKEN_TEXT) 
    {
        sb_putc(' ', sb);
    }

    sb_puts("(*)", sb);
    sb_putc('(', sb);

    XML_Token *next_param = NULL;
    bool first_param = true; 

    while ((next_param = find_next(command, "param", &tag_index))) 
    {
        if (!first_param) 
        {
            sb_puts(", ", sb);
        } 
        else 
        {
            first_param = false;
        }

        // Only write the types, not paramater names. This is more complex thad
        // I'd like but gl.xml doesn't include qualifiers such as const in the
        // type.
        write_inner_text(sb, *next_param, next_param->value.content.length - 1);

        // A bit of a hack to strip of the trailing space.
        if (sb->ptr[sb->length - 1] == ' ') 
        {
            sb->ptr[--sb->length] = '\0';
        }
    }

    sb_putc(')', sb);
}

void write_parameter_names(StringBuffer *sb, XML_Token command) 
{
    size_t param_index = 0;
    XML_Token *next_param = NULL;
    bool first_param = true; 

    while ((next_param = find_next(command, "param", &param_index))) 
    {
        if (!first_param) 
        {
            sb_puts(", ", sb);
        } 
        else 
        {
            first_param = false;
        }

        // Here we must extract the names of parameters and ignore their types.
        XML_Token *name_tag = find_next(*next_param, "name", NULL);
        write_inner_text(sb, *name_tag, -1);
    }
}

void write_body(StringBuffer *sb, XML_Token command, size_t *command_index) 
{
    XML_Token *proto = find_next(command, "proto", NULL);
    XML_Token return_type = proto->value.content.tokens[0];

    // Function body
    sb_puts("{\n    ", sb);

    // If the command doesn't return anything, the wrapper also shouldn't return
    // anything. This avoids a warning.
    if (!XML_str_eq_cstr(return_type.value.text, "void ")) 
    {
        sb_puts("return ", sb); 
    }

    sb_putc('(', sb);

    // Cast to appropriate function pointer.
    sb_putc('(', sb);
    write_as_function_ptr_type(sb, command);
    sb_putc(')', sb);

    // Lookup function pointer.
    sb_puts("(lookup[", sb);
    sb_printf(sb, "%d", (*command_index)++);
    sb_puts("].proc)", sb);

    sb_putc(')', sb);

    // Finally provide the argumets.
    sb_putc('(', sb);
    write_parameter_names(sb, command);
    sb_puts(");\n", sb);

    sb_puts("}\n\n", sb);
}

void generate_command(GenerationContext *ctx, XML_Token command) 
{
    StringBuffer *command_lookup = &ctx->command_lookup;
    StringBuffer *commands = &ctx->commands;

    size_t tag_index = 0;
    XML_Token *proto = find_next(command, "proto", &tag_index);
    XML_Token *command_name = find_next(*proto, "name", NULL);

    write_prototype(commands, command);
    sb_putc('\n', commands);
    write_body(commands, command, &ctx->command_index);

    // Append entry to command lookup
    sb_puts("    { NULL, \"", command_lookup);
    write_inner_text(command_lookup, *command_name, -1); 
    sb_puts("\" },\n", command_lookup);
}

void generate_command_define(GenerationContext *ctx, XML_Token command) 
{
    StringBuffer *defines = &ctx->command_defines;
    XML_Token *proto = find_next(command, "proto", NULL);
    XML_Token *command_name = find_next(*proto, "name", NULL);

    sb_puts("#define ", defines);
    write_inner_text(defines, *command_name, -1);
    sb_putc('(', defines);
    write_parameter_names(defines, command);
    sb_puts(") ", defines);

    sb_puts(PREFIX, defines);
    write_inner_text(defines, *command_name, -1);
    sb_putc('(', defines);
    write_parameter_names(defines, command);
    sb_puts(")\n", defines);
}

void generate_commands(GenerationContext *ctx, XML_Token *commands) 
{
    // Command lookup prelude
    sb_puts("typedef struct {\n", &ctx->command_lookup);
    sb_puts("    void (*proc)(void);\n", &ctx->command_lookup);
    sb_puts("    const char *name;\n", &ctx->command_lookup);
    sb_puts("} Proc;\n", &ctx->command_lookup);
    sb_putc('\n', &ctx->command_lookup);

    sb_puts("static Proc lookup[] = {\n", &ctx->command_lookup);

    for (size_t i = 0; i < commands->value.content.length; i++) 
    {
        XML_Token token = commands->value.content.tokens[i];
        if (token.type == XML_TOKEN_TEXT || !XML_str_eq_cstr(token.value.content.tag.name, "command")) 
        {
            continue;
        }
        generate_command(ctx, token);
        generate_command_define(ctx, token);
    }

    // Finish up command lookup.
    sb_puts("};\n", &ctx->command_lookup);
}

void write_output(FILE *file, GenerationContext ctx)
{
    fputs("/* SECTION: types */\n\n", file);
    fwrite(ctx.types.ptr, 1, ctx.types.length, file);

    fputs("/* SECTION: enums */\n\n", file);
    fwrite(ctx.enums.ptr, 1, ctx.enums.length, file);

    fputs("/* SECTION: command_lookup */\n\n", file);
    fwrite(ctx.command_lookup.ptr, 1, ctx.command_lookup.length, file);

    fputs("/* SECTION: commands */\n\n", file);
    fwrite(ctx.commands.ptr, 1, ctx.commands.length, file);

    fputs("/* SECTION: command_defines */\n\n", file);
    fwrite(ctx.command_defines.ptr, 1, ctx.command_defines.length, file);
}

void generate(FILE *file, XML_Token root) 
{
    if (root.type != XML_TOKEN_NODE) 
    {
        fprintf(stderr, "Generator error: expected node, got text!\n");
        return;
    }

    GenerationContext ctx = init_context();

    size_t section_index = 0;
    XML_Token *types_section = find_next(root, "types", &section_index);
    XML_Token *commands_section = find_next(root, "commands", &section_index);

    generate_types(&ctx, types_section);
    generate_commands(&ctx, commands_section);

    write_output(file, ctx);
    free_context(ctx);
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
