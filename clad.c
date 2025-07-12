#include "xml.h"
#include "string_buffer.h"
#include <assert.h>
#include <stdlib.h>

#define PREFIX "clad_"

#define GL_VERSIONS \
    X(GL_VERSION_1_0) \
    X(GL_VERSION_1_1) \
    X(GL_VERSION_1_2) \
    X(GL_VERSION_1_3) \
    X(GL_VERSION_1_4) \
    X(GL_VERSION_1_5) \
    X(GL_VERSION_2_0) \
    X(GL_VERSION_2_1) \
    X(GL_VERSION_3_0) \
    X(GL_VERSION_3_1) \
    X(GL_VERSION_3_2) \
    X(GL_VERSION_3_3) \
    X(GL_VERSION_4_0) \
    X(GL_VERSION_4_1) \
    X(GL_VERSION_4_2) \
    X(GL_VERSION_4_3) \
    X(GL_VERSION_4_4) \
    X(GL_VERSION_4_5) \
    X(GL_VERSION_4_6)

typedef enum {
#define X(version) version,
    GL_VERSIONS
#undef X
} GLVersion;

static GLVersion gl_version_from_sv(XML_StringView sv) 
{
#define X(version) if (XML_str_eq_cstr(sv, #version)) return version;
    GL_VERSIONS
#undef X
    return -1;
}

typedef enum {
    GL_API_GL,
    GL_API_GLES1,
    GL_API_GLES2,
    GL_API_GLSC2,
} GLAPIType;

static GLAPIType gl_api_from_sv(XML_StringView sv) 
{
    if (XML_str_eq_cstr(sv, "gl")) return GL_API_GL;
    if (XML_str_eq_cstr(sv, "gles1")) return GL_API_GLES1;
    if (XML_str_eq_cstr(sv, "gles2")) return GL_API_GLES2;
    if (XML_str_eq_cstr(sv, "glsc2")) return GL_API_GLSC2;
    return -1;
}

typedef enum {
    GL_CORE = 0,
    GL_COMPATIBILITY,
} GLProfile;

static GLProfile gl_profile_from_sv(XML_StringView sv) 
{
    if (XML_str_eq_cstr(sv, "core")) return GL_CORE;
    if (XML_str_eq_cstr(sv, "compatibility")) return GL_COMPATIBILITY;
    return -1;
}

typedef enum {
    DEF_ENUM,
    DEF_CMD,
} DefinitionType;

typedef struct {
    DefinitionType *types;
    XML_StringView *names;
    bool *required;
    size_t length;
    size_t capacity;
} RequirementList;

static RequirementList rl_init(void)
{
    RequirementList rl = {0};
    rl.capacity = 32;
    rl.types = calloc(rl.capacity, sizeof(*rl.types));
    rl.names = calloc(rl.capacity, sizeof(*rl.names));
    rl.required = calloc(rl.capacity, sizeof(*rl.required));
    return rl;
}

static void rl_add(RequirementList *rl, DefinitionType type, XML_StringView name, bool required)
{
    // First check whether the feature is in the list.
    for (size_t i = 0; i < rl->length; i++) 
    {
        if (rl->types[i] == type && XML_str_eq(rl->names[i], name))
        {
            rl->required[i] = required;
            return; 
        }
    } 

    if (rl->length >= rl->capacity) 
    {
        rl->capacity *= 2;
        rl->types = realloc(rl->types, rl->capacity * sizeof(*rl->types));
        rl->names = realloc(rl->names, rl->capacity * sizeof(*rl->names));
        rl->required = realloc(rl->required, rl->capacity * sizeof(*rl->required));
    }

    rl->types[rl->length] = type;
    rl->names[rl->length] = name;
    rl->required[rl->length] = required;
    rl->length++;
}

static void rl_free(RequirementList rl)
{
    free(rl.types);
    free(rl.names);
    free(rl.required);
}

typedef struct {
    GLAPIType api;
    GLProfile profile;
    GLVersion version;
    RequirementList requirements;

    size_t command_index;
    StringBuffer types;
    StringBuffer enums;
    StringBuffer command_lookup;
    StringBuffer commands;
    StringBuffer command_defines;
} GenerationContext;

static GenerationContext init_context(GLAPIType api, GLProfile profile, GLVersion version) 
{
    GenerationContext ctx = {0};
    ctx.api = api;
    ctx.profile = profile;
    ctx.version = version;
    ctx.requirements = rl_init();
    ctx.types = sb_new_buffer();
    ctx.enums = sb_new_buffer();
    ctx.command_lookup = sb_new_buffer();
    ctx.commands = sb_new_buffer();
    ctx.command_defines = sb_new_buffer();
    return ctx;
}

static void free_context(GenerationContext ctx)
{
    sb_free(ctx.types);
    sb_free(ctx.enums);
    sb_free(ctx.command_lookup);
    sb_free(ctx.commands);
    sb_free(ctx.command_defines);
    rl_free(ctx.requirements);
}

static XML_Token *find_next(XML_Token parent, const char *tag, size_t *index)
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

static bool sv_starts_with(XML_StringView str, const char *with) 
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

static void put_xml_string_view(StringBuffer *file, XML_StringView str) 
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

static void write_inner_text(StringBuffer *buffer, XML_Token token, int count)
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

static void generate_types(GenerationContext *ctx, XML_Token root)
{
    XML_Token *types = find_next(root, "types", NULL);
    assert(types);

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

static void write_prototype(StringBuffer *sb, XML_Token command) 
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

static void write_as_function_ptr_type(StringBuffer *sb, XML_Token command) 
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

static void write_parameter_names(StringBuffer *sb, XML_Token command) 
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

static void write_body(StringBuffer *sb, XML_Token command, size_t *command_index) 
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

static void generate_command_wrapper(GenerationContext *ctx, XML_Token command) 
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

static void generate_command_define(GenerationContext *ctx, XML_Token command) 
{
    StringBuffer *defines = &ctx->command_defines;
    XML_Token *proto = find_next(command, "proto", NULL);
    XML_Token *command_name = find_next(*proto, "name", NULL);

    sb_puts("#define ", defines);
    write_inner_text(defines, *command_name, -1);
    sb_putc(' ', defines);
    sb_puts(PREFIX, defines);
    write_inner_text(defines, *command_name, -1);
    sb_putc('\n', defines);
}

static XML_StringView get_command_name(XML_Token command)
{
    XML_Token *proto = find_next(command, "proto", NULL);
    assert(proto);
    XML_Token *name = find_next(*proto, "name", NULL);
    assert(name);

    assert(name->value.content.length == 1);
    assert(name->value.content.tokens[0].type == XML_TOKEN_TEXT);
    return name->value.content.tokens[0].value.text;
}

void generate_command(GenerationContext *ctx, XML_Token commands, XML_StringView name) 
{
    size_t cmd_index = 0;
    XML_Token *command = NULL;

    while ((command = find_next(commands, "command", &cmd_index))) 
    {
        if (XML_str_eq(get_command_name(*command), name)) 
        {
            generate_command_wrapper(ctx, *command);
            generate_command_define(ctx, *command);
            return;
        }
    }
}
    
static void write_header(GenerationContext *ctx)
{
    sb_puts("typedef struct {\n", &ctx->command_lookup);
    sb_puts("    void (*proc)(void);\n", &ctx->command_lookup);
    sb_puts("    const char *name;\n", &ctx->command_lookup);
    sb_puts("} Proc;\n\n", &ctx->command_lookup);
    sb_puts("static Proc lookup[] = {\n", &ctx->command_lookup);
}

static void write_footer(GenerationContext *ctx)
{
    sb_puts("};\n", &ctx->command_lookup);
}

static bool is_version_leq(XML_Token feature, GLAPIType expected_api, GLVersion max_version) 
{
    XML_StringView api;
    if (!XML_get_attribute(feature, "api", &api))
    {
        fprintf(stderr, "Generation error: expected attribute `api` on <feature>!\n");
        return false;
    }

    if (gl_api_from_sv(api) != expected_api) 
        return false; 

    XML_StringView version;
    if (!XML_get_attribute(feature, "name", &version))
    {
        fprintf(stderr, "Generation error: expected attribute `name` on <feature>!\n");
        return false;
    }

    if (gl_version_from_sv(version) > max_version) 
        return false; 

    return true;
}
                
static void register_require(GenerationContext *ctx, XML_Token parent, bool require) 
{
    for (size_t i = 0; i < parent.value.content.length; i++) 
    {
        XML_Token def = parent.value.content.tokens[i];

        if (def.type != XML_TOKEN_NODE) 
        {
            continue;
        }

        XML_StringView def_tag_name = def.value.content.tag.name;
        DefinitionType def_type; 

        if (XML_str_eq_cstr(def_tag_name, "enum")) 
            def_type = DEF_ENUM;
        else if (XML_str_eq_cstr(def_tag_name, "command")) 
            def_type = DEF_CMD;
        else 
            continue;

        XML_StringView name;
        if (!XML_get_attribute(def, "name", &name)) 
        {
            fprintf(stderr, "Generation error: expected `name` attribute!\n");
            continue;
        }

        rl_add(&ctx->requirements, def_type, name, require);
    }
}

// TODO: report success or failure!
static void gather_featureset(GenerationContext *ctx, XML_Token root) 
{
    size_t version_tag_index = 0;
    for (GLVersion version = GL_VERSION_1_0; version <= ctx->version; version++) 
    {
        XML_Token *feature_tag = find_next(root, "feature", &version_tag_index); 

        // There are no more <feature> tags in the file.
        if (!feature_tag)
            break;

        if (!is_version_leq(*feature_tag, ctx->api, ctx->version))
            continue;

        // This is a bit cursed, but if it works...
        for (size_t r_index = 0;;)
        {
            bool require = true;
            XML_Token *r = find_next(*feature_tag, "require", &r_index);
            if (!r) 
            {
                r = find_next(*feature_tag, "remove", &r_index);
                require = false;
            }

            // Neither <require> nor <remove> could be found, exit the loop.
            if (!r)
            {
                break; 
            }

            XML_StringView profile;
            if (XML_get_attribute(*r, "profile", &profile)) 
            {
                // TODO: Check whether one profile is a subset of the other!
                if (gl_profile_from_sv(profile) != ctx->profile) 
                {
                    continue; 
                }
            }

            // If no profile is provided, then continue processing the tag
            // regardless.
            register_require(ctx, *r, require);
        }
    }
}

static void write_output(FILE *file, GenerationContext ctx)
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

static XML_StringView get_enum_name(XML_Token _enum) 
{
    XML_StringView name;
    bool found_name = XML_get_attribute(_enum, "name", &name);
    assert(found_name);
    return name;
}

static XML_StringView get_enum_value(XML_Token _enum)
{
    XML_StringView value;
    bool found_value = XML_get_attribute(_enum, "value", &value);
    assert(found_value);
    return value;
}

static void generate_enum(GenerationContext *ctx, XML_Token root, XML_StringView name) 
{
    size_t enums_index = 0;
    XML_Token *enums = NULL;

    while ((enums = find_next(root, "enums", &enums_index))) 
    {
        size_t enum_index = 0;
        XML_Token *_enum = NULL;

        while ((_enum = find_next(*enums, "enum", &enum_index))) 
        {
            XML_StringView enum_name = get_enum_name(*_enum);

            if (!XML_str_eq(enum_name, name)) 
            {
                continue; 
            }

            XML_StringView enum_value = get_enum_value(*_enum);

            sb_puts("#define ", &ctx->enums);
            sb_putsn(&ctx->enums, enum_name.start, enum_name.length);
            sb_putc(' ', &ctx->enums);
            sb_putsn(&ctx->enums, enum_value.start, enum_value.length);
            sb_putc('\n', &ctx->enums);
        }
    }
}

static void generate(FILE *file, XML_Token root, GLAPIType api, GLProfile profile, GLVersion version) 
{
    assert(root.type == XML_TOKEN_NODE);

    GenerationContext ctx = init_context(api, profile, version);
    generate_types(&ctx, root);
    gather_featureset(&ctx, root);
    write_header(&ctx);

    XML_Token *commands = find_next(root, "commands", NULL);
    assert(commands);

    for (size_t i = 0; i < ctx.requirements.length; i++) 
    {
        if (!ctx.requirements.required[i]) 
        {
            continue;
        }

        switch (ctx.requirements.types[i]) 
        {
            case DEF_ENUM:
                generate_enum(&ctx, root, ctx.requirements.names[i]);
                break;
            case DEF_CMD:
                generate_command(&ctx, *commands, ctx.requirements.names[i]);
                break;
        }
    }

    write_footer(&ctx);
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
        // XML_read_file will have reported the error.
        return 1;
    }

    if (XML_parse_file(src, &root)) 
    {
        // TODO: Don't hardcode this!
        generate(stdout, root, GL_API_GL, GL_CORE, GL_VERSION_3_3);
        XML_free(root);
    }

    free(src);
    return 0;
}
