#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "string_buffer.h"
#include "string_view.h"
#include "template.h"
#include "xml.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>

#define GL_VERSIONS                                                            \
    X(GL_VERSION_1_0, 1.0)                                                     \
    X(GL_VERSION_1_1, 1.1)                                                     \
    X(GL_VERSION_1_2, 1.2)                                                     \
    X(GL_VERSION_1_3, 1.3)                                                     \
    X(GL_VERSION_1_4, 1.4)                                                     \
    X(GL_VERSION_1_5, 1.5)                                                     \
    X(GL_VERSION_2_0, 2.0)                                                     \
    X(GL_VERSION_2_1, 2.1)                                                     \
    X(GL_VERSION_3_0, 3.0)                                                     \
    X(GL_VERSION_3_1, 3.1)                                                     \
    X(GL_VERSION_3_2, 3.2)                                                     \
    X(GL_VERSION_3_3, 3.3)                                                     \
    X(GL_VERSION_4_0, 4.0)                                                     \
    X(GL_VERSION_4_1, 4.1)                                                     \
    X(GL_VERSION_4_2, 4.2)                                                     \
    X(GL_VERSION_4_3, 4.3)                                                     \
    X(GL_VERSION_4_4, 4.4)                                                     \
    X(GL_VERSION_4_5, 4.5)                                                     \
    X(GL_VERSION_4_6, 4.6)

typedef enum {
#define X(version, short) version,
    GL_VERSIONS
#undef X
        GL_VERSION_INVALID,
} GLVersion;

static GLVersion gl_version_from_sv(StringView sv) {
#define X(version, short)                                                      \
    if (sv_equal_cstr(sv, #version))                                           \
        return version;
    GL_VERSIONS
#undef X
    return GL_VERSION_INVALID;
}

static GLVersion gl_version_from_sv_short(StringView sv) {
#define X(version, short)                                                      \
    if (sv_equal_cstr(sv, #short))                                             \
        return version;
    GL_VERSIONS
#undef X
    return GL_VERSION_INVALID;
}

typedef enum {
    GL_API_GL,
    GL_API_GLES1,
    GL_API_GLES2,
    GL_API_GLSC2,
    GL_API_INVALID,
} GLAPIType;

static GLAPIType gl_api_from_sv(StringView sv) {
    if (sv_equal_cstr(sv, "gl"))
        return GL_API_GL;
    if (sv_equal_cstr(sv, "gles1"))
        return GL_API_GLES1;
    if (sv_equal_cstr(sv, "gles2"))
        return GL_API_GLES2;
    if (sv_equal_cstr(sv, "glsc2"))
        return GL_API_GLSC2;
    return GL_API_INVALID;
}

typedef enum {
    GL_PROFILE_CORE,
    GL_PROFILE_COMPATIBILITY,
    GL_PROFILE_INVALID,
} GLProfile;

static GLProfile gl_profile_from_sv(StringView sv) {
    if (sv_equal_cstr(sv, "core"))
        return GL_PROFILE_CORE;
    if (sv_equal_cstr(sv, "compatibility"))
        return GL_PROFILE_COMPATIBILITY;
    return GL_PROFILE_INVALID;
}

typedef struct {
    const char *input_xml;
    const char *output_header;
    const char *output_source;
    const char *api;
    const char *profile;
    const char *version;
    const char *header_template;
    const char *source_template;
    bool use_snake_case;
} RawArguments;

typedef struct {
    const char *input_xml;
    const char *output_header;
    const char *output_source;
    const char *header_template_path;
    const char *source_template_path;
    GLAPIType api;
    GLProfile profile;
    GLVersion version;
    bool use_snake_case;

    bool parsed_succesfully;
} CladOptions;

typedef enum {
    DEF_ENUM,
    DEF_CMD,
} DefinitionType;

typedef struct {
    DefinitionType *types;
    StringView *names;
    bool *required;
    size_t length;
    size_t capacity;
} RequirementList;

static RequirementList rl_init(void) {
    RequirementList rl = { 0 };
    rl.capacity = 32;
    rl.types = calloc(rl.capacity, sizeof(*rl.types));
    rl.names = calloc(rl.capacity, sizeof(*rl.names));
    rl.required = calloc(rl.capacity, sizeof(*rl.required));
    return rl;
}

static void rl_add(RequirementList *rl, DefinitionType type, StringView name,
                   bool required) {
    // First check whether the feature is in the list.
    for (size_t i = 0; i < rl->length; i++) {
        if (rl->types[i] == type && sv_equal(rl->names[i], name)) {
            rl->required[i] = required;
            return;
        }
    }

    if (rl->length >= rl->capacity) {
        rl->capacity *= 2;
        rl->types = realloc(rl->types, rl->capacity * sizeof(*rl->types));
        rl->names = realloc(rl->names, rl->capacity * sizeof(*rl->names));
        rl->required =
            realloc(rl->required, rl->capacity * sizeof(*rl->required));
    }

    rl->types[rl->length] = type;
    rl->names[rl->length] = name;
    rl->required[rl->length] = required;
    rl->length++;
}

static void rl_free(RequirementList rl) {
    free(rl.types);
    free(rl.names);
    free(rl.required);
}

typedef struct {
    bool use_snake_case;

    GLAPIType api;
    GLProfile profile;
    GLVersion version;
    RequirementList requirements;

    size_t command_index;
    StringBuffer types;
    StringBuffer enums;
    StringBuffer command_lookup;
    StringBuffer command_wrappers;
    StringBuffer command_decls;

    const char *header_template_path;
    const char *source_template_path;
    FILE *output_header;
    FILE *output_source;
} GenerationContext;

static GenerationContext init_context(CladOptions opts, FILE *output_header,
                                      FILE *output_source) {
    GenerationContext ctx = { 0 };
    ctx.use_snake_case = opts.use_snake_case;
    ctx.api = opts.api;
    ctx.profile = opts.profile;
    ctx.version = opts.version;
    ctx.requirements = rl_init();
    ctx.types = sb_new_buffer();
    ctx.enums = sb_new_buffer();
    ctx.command_lookup = sb_new_buffer();
    ctx.command_wrappers = sb_new_buffer();
    ctx.command_decls = sb_new_buffer();
    ctx.header_template_path = opts.header_template_path;
    ctx.source_template_path = opts.source_template_path;
    ctx.output_header = output_header;
    ctx.output_source = output_source;
    return ctx;
}

static void free_context(GenerationContext ctx) {
    sb_free(ctx.types);
    sb_free(ctx.enums);
    sb_free(ctx.command_lookup);
    sb_free(ctx.command_wrappers);
    sb_free(ctx.command_decls);
    rl_free(ctx.requirements);
}

static xml_Token *find_next(xml_Token parent, const char *tag, size_t *index) {
    size_t local_index = 0;

    if (index == NULL) {
        index = &local_index;
    }

    while (*index < parent.value.content.length) {
        xml_Token *child = &parent.value.content.tokens[(*index)++];
        if (sv_equal_cstr(child->value.content.tag.name, tag)) {
            return child;
        }
    }
    return NULL;
}

static void put_xml_string_view(StringBuffer *file, StringView str) {
    size_t i = 0;
    while (i < str.length) {
        if (str.start[i] == '&') {
            StringView substr = { .start = &str.start[i],
                                  .length = str.length - i };

            if (sv_starts_with_cstr(substr, "&quot;")) {
                sb_putc('"', file);
                i += 6;
                continue;
            } else if (sv_starts_with_cstr(substr, "&apos;")) {
                sb_putc('\'', file);
                i += 6;
                continue;
            } else if (sv_starts_with_cstr(substr, "&lt;")) {
                sb_putc('<', file);
                i += 4;
                continue;
            } else if (sv_starts_with_cstr(substr, "&gt;")) {
                sb_putc('>', file);
                i += 4;
                continue;
            } else if (sv_starts_with_cstr(substr, "&amp;")) {
                sb_putc('&', file);
                i += 5;
                continue;
            }
        }

        sb_putc(str.start[i], file);
        i++;
    }
}

static void write_inner_text(StringBuffer *buffer, xml_Token token, int count) {
    switch (token.type) {
    case XML_TOKEN_TEXT:
        put_xml_string_view(buffer, token.value.text);
        break;
    case XML_TOKEN_NODE: {
        size_t max = (count < 0) ? token.value.content.length : (size_t)count;
        for (size_t i = 0; i < max; i++) {
            write_inner_text(buffer, token.value.content.tokens[i], -1);
        }
        break;
    }
    }
}

static void generate_types(GenerationContext *ctx, xml_Token root) {
    xml_Token *types = find_next(root, "types", NULL);
    assert(types);

    for (size_t i = 0; i < types->value.content.length; i++) {
        xml_Token token = types->value.content.tokens[i];
        if (token.type == XML_TOKEN_TEXT ||
            !sv_equal_cstr(token.value.content.tag.name, "type")) {
            continue;
        }

        write_inner_text(&ctx->types, token, -1);
        sb_putc('\n', &ctx->types);
    }
}

static void write_snake_case(StringBuffer *sb, StringView name) {
    char previous = '\0';
    for (size_t i = 0; i < name.length; i++) {
        char ch = name.start[i];

        if (isupper(ch) && islower(previous)) {
            sb_putc('_', sb);
            sb_putc(ch + 32, sb); // TODO: magic number
        } else {
            sb_putc(ch, sb);
        }

        previous = ch;
    }
}

static void write_prototype(StringBuffer *sb, xml_Token command,
                            bool snake_case) {
    size_t tag_index = 0;
    xml_Token *proto = find_next(command, "proto", &tag_index);
    assert(proto);

    xml_Token *command_name_token = find_next(*proto, "name", NULL);
    assert(command_name_token);
    assert(command_name_token->value.content.length == 1);
    assert(command_name_token->value.content.tokens[0].type == XML_TOKEN_TEXT);

    // Write return type
    write_inner_text(sb, *proto, proto->value.content.length - 1);

    // Write function name
    if (snake_case) {
        StringView command_name =
            command_name_token->value.content.tokens[0].value.text;
        write_snake_case(sb, command_name);
    } else {
        write_inner_text(sb, *command_name_token, -1);
    }

    // Function parameters
    sb_putc('(', sb);

    xml_Token *next_param = NULL;
    bool first_param = true;

    while ((next_param = find_next(command, "param", &tag_index))) {
        if (first_param) {
            first_param = false;
        } else {
            sb_puts(", ", sb);
        }

        write_inner_text(sb, *next_param, -1);
    }

    // Function doesn't have any parameters
    if (first_param) {
        sb_puts("void", sb);
    }

    sb_puts(")", sb);
}

static void write_as_function_ptr_type(StringBuffer *sb, xml_Token command) {
    size_t tag_index = 0;
    xml_Token *proto = find_next(command, "proto", &tag_index);
    assert(proto);

    // Write return type.
    write_inner_text(sb, *proto, proto->value.content.length - 1);

    sb_puts("(*)", sb);
    sb_putc('(', sb);

    xml_Token *next_param = NULL;
    bool first_param = true;

    while ((next_param = find_next(command, "param", &tag_index))) {
        if (!first_param) {
            sb_puts(", ", sb);
        } else {
            first_param = false;
        }

        // Only write the types, not paramater names. This is more complex thad
        // I'd like but gl.xml doesn't include qualifiers such as const in the
        // type.
        write_inner_text(sb, *next_param, next_param->value.content.length - 1);

        // A bit of a hack to strip of the trailing space.
        if (sb->ptr[sb->length - 1] == ' ') {
            sb->ptr[--sb->length] = '\0';
        }
    }

    // Function doesn't have any parameters
    if (first_param) {
        sb_puts("void", sb);
    }

    sb_putc(')', sb);
}

static void write_parameter_names(StringBuffer *sb, xml_Token command) {
    size_t param_index = 0;
    xml_Token *next_param = NULL;
    bool first_param = true;

    while ((next_param = find_next(command, "param", &param_index))) {
        if (!first_param) {
            sb_puts(", ", sb);
        } else {
            first_param = false;
        }

        // Here we must extract the names of parameters and ignore their types.
        xml_Token *name_tag = find_next(*next_param, "name", NULL);
        write_inner_text(sb, *name_tag, -1);
    }
}

static void write_body(StringBuffer *sb, xml_Token command,
                       size_t *command_index) {
    xml_Token *proto = find_next(command, "proto", NULL);
    xml_Token return_type = proto->value.content.tokens[0];

    // Function body
    sb_puts("{\n    ", sb);

    // If the command doesn't return anything, the wrapper also shouldn't return
    // anything. This avoids a warning.
    if (!sv_equal_cstr(return_type.value.text, "void ")) {
        sb_puts("return ", sb);
    }

    sb_putc('(', sb);

    // Cast to appropriate function pointer.
    sb_putc('(', sb);
    write_as_function_ptr_type(sb, command);
    sb_putc(')', sb);

    // Lookup function pointer.
    sb_puts("(lookup[", sb);
    // sb_printf(sb, "%d", (int)*command_index);
    char index_buffer[64];
    snprintf(index_buffer, sizeof(index_buffer), "%d", (int)*command_index);
    (*command_index)++;
    sb_puts(index_buffer, sb);
    sb_puts("].proc)", sb);

    sb_putc(')', sb);

    // Finally provide the argumets.
    sb_putc('(', sb);
    write_parameter_names(sb, command);
    sb_puts(");\n", sb);

    sb_puts("}\n\n", sb);
}

static void generate_command_wrapper(GenerationContext *ctx,
                                     xml_Token command) {
    size_t tag_index = 0;
    xml_Token *proto = find_next(command, "proto", &tag_index);
    xml_Token *command_name = find_next(*proto, "name", NULL);

    write_prototype(&ctx->command_wrappers, command, ctx->use_snake_case);
    write_body(&ctx->command_wrappers, command, &ctx->command_index);

    // Append entry to command lookup
    sb_puts("    { NULL, \"", &ctx->command_lookup);
    write_inner_text(&ctx->command_lookup, *command_name, -1);
    sb_puts("\" },\n", &ctx->command_lookup);
}

static void generate_command_declaration(GenerationContext *ctx,
                                         xml_Token command) {
    write_prototype(&ctx->command_decls, command, ctx->use_snake_case);
    sb_puts(";\n", &ctx->command_decls);
}

static StringView get_command_name(xml_Token command) {
    xml_Token *proto = find_next(command, "proto", NULL);
    assert(proto);
    xml_Token *name = find_next(*proto, "name", NULL);
    assert(name);

    assert(name->value.content.length == 1);
    assert(name->value.content.tokens[0].type == XML_TOKEN_TEXT);
    return name->value.content.tokens[0].value.text;
}

void generate_command(GenerationContext *ctx, xml_Token commands,
                      StringView name) {
    size_t cmd_index = 0;
    xml_Token *command = NULL;

    while ((command = find_next(commands, "command", &cmd_index))) {
        if (sv_equal(get_command_name(*command), name)) {
            generate_command_wrapper(ctx, *command);
            generate_command_declaration(ctx, *command);
            return;
        }
    }
}

static bool is_version_leq(xml_Token feature, GLAPIType expected_api,
                           GLVersion max_version) {
    StringView api;
    if (!xml_get_attribute(feature, "api", &api)) {
        fprintf(stderr,
                "Generation error: expected attribute `api` on <feature>!\n");
        return false;
    }

    if (gl_api_from_sv(api) != expected_api)
        return false;

    StringView version;
    if (!xml_get_attribute(feature, "name", &version)) {
        fprintf(stderr,
                "Generation error: expected attribute `name` on <feature>!\n");
        return false;
    }

    if (gl_version_from_sv(version) > max_version)
        return false;

    return true;
}

static void register_require(GenerationContext *ctx, xml_Token parent,
                             bool require) {
    for (size_t i = 0; i < parent.value.content.length; i++) {
        xml_Token def = parent.value.content.tokens[i];

        if (def.type != XML_TOKEN_NODE) {
            continue;
        }

        StringView def_tag_name = def.value.content.tag.name;
        DefinitionType def_type;

        if (sv_equal_cstr(def_tag_name, "enum"))
            def_type = DEF_ENUM;
        else if (sv_equal_cstr(def_tag_name, "command"))
            def_type = DEF_CMD;
        else
            continue;

        StringView name;
        if (!xml_get_attribute(def, "name", &name)) {
            fprintf(stderr, "Generation error: expected `name` attribute!\n");
            continue;
        }

        rl_add(&ctx->requirements, def_type, name, require);
    }
}

static void gather_featureset(GenerationContext *ctx, xml_Token root) {
    size_t version_tag_index = 0;
    for (GLVersion version = GL_VERSION_1_0; version <= ctx->version;
         version++) {
        xml_Token *feature_tag = find_next(root, "feature", &version_tag_index);

        // There are no more <feature> tags in the file.
        if (!feature_tag)
            break;

        if (!is_version_leq(*feature_tag, ctx->api, ctx->version))
            continue;

        // This is a bit cursed, but if it works...
        for (size_t r_index = 0;;) {
            bool require = true;
            xml_Token *r = find_next(*feature_tag, "require", &r_index);
            if (!r) {
                r = find_next(*feature_tag, "remove", &r_index);
                require = false;
            }

            // Neither <require> nor <remove> could be found, exit the loop.
            if (!r) {
                break;
            }

            StringView profile;
            if (xml_get_attribute(*r, "profile", &profile)) {
                // TODO: Check whether one profile is a subset of the other!
                if (gl_profile_from_sv(profile) != ctx->profile) {
                    continue;
                }
            }

            // If no profile is provided, then continue processing the tag
            // regardless.
            register_require(ctx, *r, require);
        }
    }
}

static StringView into_string_view(StringBuffer str) {
    return (StringView){
        .start = str.ptr,
        .length = str.length,
    };
}

static void write_output_header(GenerationContext ctx) {
    Template template = { 0 };

    template_define(&template, "TYPES", into_string_view(ctx.types));
    template_define(&template, "ENUMS", into_string_view(ctx.enums));
    template_define(&template, "COMMAND_DECLARATIONS",
                    into_string_view(ctx.command_decls));

    char *template_str = xml_read_file(ctx.header_template_path);
    StringBuffer built = template_build(&template, template_str);
    free(template_str);

    fwrite(built.ptr, sizeof(*built.ptr), built.length, ctx.output_header);

    template_free(&template);
    sb_free(built);
}

static void write_output_source(GenerationContext ctx) {
    Template template = { 0 };

    template_define(&template, "COMMAND_LOOKUP",
                    into_string_view(ctx.command_lookup));
    template_define(&template, "COMMAND_WRAPPERS",
                    into_string_view(ctx.command_wrappers));

    char *template_str = xml_read_file(ctx.source_template_path);
    StringBuffer built = template_build(&template, template_str);
    free(template_str);

    fwrite(built.ptr, sizeof(*built.ptr), built.length, ctx.output_source);

    template_free(&template);
    sb_free(built);
}

static StringView get_enum_name(xml_Token _enum) {
    StringView name;
    bool found_name = xml_get_attribute(_enum, "name", &name);
    assert(found_name);
    return name;
}

static StringView get_enum_value(xml_Token _enum) {
    StringView value;
    bool found_value = xml_get_attribute(_enum, "value", &value);
    assert(found_value);
    return value;
}

static void generate_enum(GenerationContext *ctx, xml_Token root,
                          StringView name) {
    size_t enums_index = 0;
    xml_Token *enums = NULL;

    while ((enums = find_next(root, "enums", &enums_index))) {
        size_t enum_index = 0;
        xml_Token *_enum = NULL;

        while ((_enum = find_next(*enums, "enum", &enum_index))) {
            StringView enum_name = get_enum_name(*_enum);

            if (!sv_equal(enum_name, name)) {
                continue;
            }

            StringView enum_value = get_enum_value(*_enum);

            sb_puts("#define ", &ctx->enums);
            sb_putsn(&ctx->enums, enum_name.start, enum_name.length);
            sb_putc(' ', &ctx->enums);
            sb_putsn(&ctx->enums, enum_value.start, enum_value.length);
            sb_putc('\n', &ctx->enums);
        }
    }
}

static void generate(xml_Token root, CladOptions args, FILE *output_header,
                     FILE *output_source) {
    assert(root.type == XML_TOKEN_NODE);

    GenerationContext ctx = init_context(args, output_header, output_source);
    generate_types(&ctx, root);
    gather_featureset(&ctx, root);

    xml_Token *commands = find_next(root, "commands", NULL);
    assert(commands);

    for (size_t i = 0; i < ctx.requirements.length; i++) {
        if (!ctx.requirements.required[i]) {
            continue;
        }

        switch (ctx.requirements.types[i]) {
        case DEF_ENUM:
            generate_enum(&ctx, root, ctx.requirements.names[i]);
            break;
        case DEF_CMD:
            generate_command(&ctx, *commands, ctx.requirements.names[i]);
            break;
        }
    }

    write_output_header(ctx);
    write_output_source(ctx);
    free_context(ctx);
}

static char *shift_arguments(char ***argv) {
    char *next_string = **argv;
    if (next_string != NULL) {
        (*argv)++;
    }
    return next_string;
}

static StringView sv_from_cstr(const char *str) {
    StringView sv;
    sv.start = str;
    sv.length = convenient_strlen(str);
    return sv;
}

typedef enum { ARG_STRING, ARG_BOOL } ArgType;

typedef struct {
    ArgType type;
    const char *flag;
    void *dest;
    bool optional;
    bool found;
} Arg;

static void print_arg(Arg arg, FILE *fp) {
    fputs(arg.flag, fp);
    if (arg.type == ARG_STRING) {
        fputs(" <string>", fp);
    }
}

static void print_clad_usage(Arg *arguments, size_t arg_count) {
    fprintf(stderr, "Expected arguments:\n");

    for (size_t i = 0; i < arg_count; i++) {
        Arg arg = arguments[i];
        fputc('\t', stderr);

        if (arg.optional) {
            fputc('[', stderr);
        }

        print_arg(arg, stderr);

        if (arg.optional) {
            fputc(']', stderr);
        }

        fputc('\n', stderr);
    }
}

static bool parse_args(Arg *arguments, size_t count, char **argv) {
    shift_arguments(&argv);
    const char *next = NULL;

    while ((next = shift_arguments(&argv))) {
        if (convenient_streq(next, "\\")) {
            continue;
        }

        for (size_t i = 0; i < count; i++) {
            Arg *arg = &arguments[i];

            if (!convenient_streq(next, arg->flag)) {
                continue;
            }

            switch (arg->type) {
            case ARG_STRING:
                *(char **)arg->dest = shift_arguments(&argv);
                break;
            case ARG_BOOL:
                *(bool *)arg->dest = true;
                break;
            }

            arg->found = true;
            break;
        }
    }

    bool perfect_parse = true;

    for (size_t i = 0; i < count; i++) {
        Arg arg = arguments[i];
        if (arg.optional) {
            continue;
        }

        if (!arg.found) {
            fputs("error: missing argument: ", stderr);
            print_arg(arg, stderr);
            fputc('\n', stderr);
            perfect_parse = false;
        }
    }

    return perfect_parse;
}

static CladOptions parse_raw_args(RawArguments raw_args) {
    CladOptions opts = {
        .input_xml = raw_args.input_xml,
        .output_header = raw_args.output_header,
        .output_source = raw_args.output_source,
        .header_template_path = raw_args.header_template,
        .source_template_path = raw_args.source_template,
        .use_snake_case = raw_args.use_snake_case,
        .parsed_succesfully = true,
    };

    // Parse OpenGL API
    opts.api = gl_api_from_sv(sv_from_cstr(raw_args.api));
    if (opts.api == GL_API_INVALID) {
        fprintf(stderr, "error: failed to parse API version: %s\n",
                raw_args.api);
        opts.parsed_succesfully = false;
    }

    // Parse OpenGL profile
    opts.profile = gl_profile_from_sv(sv_from_cstr(raw_args.profile));
    if (opts.profile == GL_PROFILE_INVALID) {
        fprintf(stderr, "error: failed to parse profile: %s\n",
                raw_args.profile);
        opts.parsed_succesfully = false;
    }

    // Parse OpenGL version
    opts.version = gl_version_from_sv_short(sv_from_cstr(raw_args.version));
    if (opts.version == GL_VERSION_INVALID) {
        fprintf(stderr, "error: failed to parse version: %s\n",
                raw_args.version);
        opts.parsed_succesfully = false;
    }

    return opts;
}

static CladOptions parse_commandline_arguments(char **argv) {
    RawArguments raw_args = { 0 };

    Arg arguments[] = {
        {
            .type = ARG_BOOL,
            .flag = "--snake-case",
            .optional = true,
            .dest = &raw_args.use_snake_case,
        },
        {
            .type = ARG_STRING,
            .flag = "--in-xml",
            .dest = &raw_args.input_xml,
        },
        {
            .type = ARG_STRING,
            .flag = "--out-header",
            .dest = &raw_args.output_header,
        },
        {
            .type = ARG_STRING,
            .flag = "--out-source",
            .dest = &raw_args.output_source,
        },
        {
            .type = ARG_STRING,
            .flag = "--api",
            .dest = &raw_args.api,
        },
        {
            .type = ARG_STRING,
            .flag = "--profile",
            .dest = &raw_args.profile,
        },
        {
            .type = ARG_STRING,
            .flag = "--version",
            .dest = &raw_args.version,
        },
        {
            .type = ARG_STRING,
            .flag = "--header-template",
            .dest = &raw_args.header_template,
        },
        {
            .type = ARG_STRING,
            .flag = "--source-template",
            .dest = &raw_args.source_template,
        },
    };

    size_t arg_count = sizeof(arguments) / sizeof(*arguments);

    CladOptions opts = { 0 };
    if (!parse_args(arguments, arg_count, argv)) {
        return opts;
    }

    opts = parse_raw_args(raw_args);

    if (!opts.parsed_succesfully) {
        print_clad_usage(arguments, arg_count);
    }

    return opts;
}

static FILE *try_to_open(const char *path, const char *mode) {
    FILE *fp = fopen(path, mode);

    if (fp == NULL) {
        fprintf(stderr, "error: couldn't open `%s`!\n", path);
    }

    return fp;
}

static void try_to_close(FILE *fp) {
    if (fp != NULL) {
        fclose(fp);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    int ret = EXIT_FAILURE;

    CladOptions opts = parse_commandline_arguments(argv);
    if (!opts.parsed_succesfully) {
        return ret;
    }

    FILE *output_header = try_to_open(opts.output_header, "w");
    FILE *output_source = try_to_open(opts.output_source, "w");

    if (output_header && output_source) {
        xml_Token root;
        char *src = xml_read_file(opts.input_xml);
        if (!src)
            goto failure;

        if (xml_parse_file(src, &root)) {
            generate(root, opts, output_header, output_source);
            xml_free(root);
        }

        free(src);
        ret = EXIT_SUCCESS;
    }

failure:
    try_to_close(output_header);
    try_to_close(output_source);
    return ret;
}
