#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "string_buffer.h"
#include "string_view.h"
#include "xml.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>

#define PREFIX "clad_"

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

static GLVersion gl_version_from_sv(XML_StringView sv) {
#define X(version, short)                                                      \
    if (XML_str_eq_cstr(sv, #version))                                         \
        return version;
    GL_VERSIONS
#undef X
    return GL_VERSION_INVALID;
}

static GLVersion gl_version_from_sv_short(XML_StringView sv) {
#define X(version, short)                                                      \
    if (XML_str_eq_cstr(sv, #short))                                           \
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

static GLAPIType gl_api_from_sv(XML_StringView sv) {
    if (XML_str_eq_cstr(sv, "gl"))
        return GL_API_GL;
    if (XML_str_eq_cstr(sv, "gles1"))
        return GL_API_GLES1;
    if (XML_str_eq_cstr(sv, "gles2"))
        return GL_API_GLES2;
    if (XML_str_eq_cstr(sv, "glsc2"))
        return GL_API_GLSC2;
    return GL_API_INVALID;
}

typedef enum {
    GL_PROFILE_CORE,
    GL_PROFILE_COMPATIBILITY,
    GL_PROFILE_INVALID,
} GLProfile;

static GLProfile gl_profile_from_sv(XML_StringView sv) {
    if (XML_str_eq_cstr(sv, "core"))
        return GL_PROFILE_CORE;
    if (XML_str_eq_cstr(sv, "compatibility"))
        return GL_PROFILE_COMPATIBILITY;
    return GL_PROFILE_INVALID;
}

typedef struct {
    const char *input_xml;
    const char *output_header;
    const char *output_source;
    GLAPIType api;
    GLProfile profile;
    GLVersion version;

    // Optional
    bool use_snake_case;

    bool parsed_succesfully;
} CladArguments;

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

static RequirementList rl_init(void) {
    RequirementList rl = { 0 };
    rl.capacity = 32;
    rl.types = calloc(rl.capacity, sizeof(*rl.types));
    rl.names = calloc(rl.capacity, sizeof(*rl.names));
    rl.required = calloc(rl.capacity, sizeof(*rl.required));
    return rl;
}

static void rl_add(RequirementList *rl, DefinitionType type,
                   XML_StringView name, bool required) {
    // First check whether the feature is in the list.
    for (size_t i = 0; i < rl->length; i++) {
        if (rl->types[i] == type && XML_str_eq(rl->names[i], name)) {
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
    StringBuffer command_defines;
    StringBuffer command_prototypes;

    FILE *output_header;
    FILE *output_source;
} GenerationContext;

static GenerationContext init_context(bool use_snake_case, GLAPIType api,
                                      GLProfile profile, GLVersion version,
                                      FILE *output_header,
                                      FILE *output_source) {
    GenerationContext ctx = { 0 };
    ctx.use_snake_case = use_snake_case;
    ctx.api = api;
    ctx.profile = profile;
    ctx.version = version;
    ctx.requirements = rl_init();
    ctx.types = sb_new_buffer();
    ctx.enums = sb_new_buffer();
    ctx.command_lookup = sb_new_buffer();
    ctx.command_wrappers = sb_new_buffer();
    ctx.command_defines = sb_new_buffer();
    ctx.command_prototypes = sb_new_buffer();
    ctx.output_header = output_header;
    ctx.output_source = output_source;
    return ctx;
}

static void free_context(GenerationContext ctx) {
    sb_free(ctx.types);
    sb_free(ctx.enums);
    sb_free(ctx.command_lookup);
    sb_free(ctx.command_wrappers);
    sb_free(ctx.command_defines);
    sb_free(ctx.command_prototypes);
    rl_free(ctx.requirements);
}

static XML_Token *find_next(XML_Token parent, const char *tag, size_t *index) {
    size_t local_index = 0;

    if (index == NULL) {
        index = &local_index;
    }

    while (*index < parent.value.content.length) {
        XML_Token *child = &parent.value.content.tokens[(*index)++];
        if (XML_str_eq_cstr(child->value.content.tag.name, tag)) {
            return child;
        }
    }
    return NULL;
}

static bool sv_starts_with(XML_StringView str, const char *with) {
    for (size_t i = 0; i < str.length; i++) {
        if (with[i] == '\0') {
            break;
        }
        if (str.start[i] != with[i]) {
            return false;
        }
    }

    return true;
}

static void put_xml_string_view(StringBuffer *file, XML_StringView str) {
    size_t i = 0;
    while (i < str.length) {
        if (str.start[i] == '&') {
            XML_StringView substr = { .start = &str.start[i],
                                      .length = str.length - i };

            if (sv_starts_with(substr, "&quot;")) {
                sb_putc('"', file);
                i += 6;
                continue;
            } else if (sv_starts_with(substr, "&apos;")) {
                sb_putc('\'', file);
                i += 6;
                continue;
            } else if (sv_starts_with(substr, "&lt;")) {
                sb_putc('<', file);
                i += 4;
                continue;
            } else if (sv_starts_with(substr, "&gt;")) {
                sb_putc('>', file);
                i += 4;
                continue;
            } else if (sv_starts_with(substr, "&amp;")) {
                sb_putc('&', file);
                i += 5;
                continue;
            }
        }

        sb_putc(str.start[i], file);
        i++;
    }
}

static void write_inner_text(StringBuffer *buffer, XML_Token token, int count) {
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

static void generate_types(GenerationContext *ctx, XML_Token root) {
    XML_Token *types = find_next(root, "types", NULL);
    assert(types);

    for (size_t i = 0; i < types->value.content.length; i++) {
        XML_Token token = types->value.content.tokens[i];
        if (token.type == XML_TOKEN_TEXT ||
            !XML_str_eq_cstr(token.value.content.tag.name, "type")) {
            continue;
        }

        write_inner_text(&ctx->types, token, -1);
        sb_putc('\n', &ctx->types);
    }
}

static void write_prototype(StringBuffer *sb, XML_Token command) {
    size_t tag_index = 0;
    XML_Token *proto = find_next(command, "proto", &tag_index);
    assert(proto);
    XML_Token *command_name = find_next(*proto, "name", NULL);
    assert(command_name);

    // Write return type
    write_inner_text(sb, *proto, proto->value.content.length - 1);

    // Write function name
    sb_puts(PREFIX, sb);
    write_inner_text(sb, *command_name, -1);

    // Function parameters
    sb_putc('(', sb);

    XML_Token *next_param = NULL;
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

static void write_as_function_ptr_type(StringBuffer *sb, XML_Token command) {
    size_t tag_index = 0;
    XML_Token *proto = find_next(command, "proto", &tag_index);
    assert(proto);

    // Write return type.
    write_inner_text(sb, *proto, proto->value.content.length - 1);

    sb_puts("(*)", sb);
    sb_putc('(', sb);

    XML_Token *next_param = NULL;
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

static void write_parameter_names(StringBuffer *sb, XML_Token command) {
    size_t param_index = 0;
    XML_Token *next_param = NULL;
    bool first_param = true;

    while ((next_param = find_next(command, "param", &param_index))) {
        if (!first_param) {
            sb_puts(", ", sb);
        } else {
            first_param = false;
        }

        // Here we must extract the names of parameters and ignore their types.
        XML_Token *name_tag = find_next(*next_param, "name", NULL);
        write_inner_text(sb, *name_tag, -1);
    }
}

static void write_body(StringBuffer *sb, XML_Token command,
                       size_t *command_index) {
    XML_Token *proto = find_next(command, "proto", NULL);
    XML_Token return_type = proto->value.content.tokens[0];

    // Function body
    sb_puts("{\n    ", sb);

    // If the command doesn't return anything, the wrapper also shouldn't return
    // anything. This avoids a warning.
    if (!XML_str_eq_cstr(return_type.value.text, "void ")) {
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
                                     XML_Token command) {
    size_t tag_index = 0;
    XML_Token *proto = find_next(command, "proto", &tag_index);
    XML_Token *command_name = find_next(*proto, "name", NULL);

    write_prototype(&ctx->command_wrappers, command);
    sb_putc('\n', &ctx->command_wrappers);
    write_body(&ctx->command_wrappers, command, &ctx->command_index);

    // Append entry to command lookup
    sb_puts("    { NULL, \"", &ctx->command_lookup);
    write_inner_text(&ctx->command_lookup, *command_name, -1);
    sb_puts("\" },\n", &ctx->command_lookup);
}

static void generate_command_declaration(GenerationContext *ctx,
                                         XML_Token command) {
    write_prototype(&ctx->command_prototypes, command);
    sb_puts(";\n", &ctx->command_prototypes);
}

static void write_snake_case(StringBuffer *sb, XML_StringView name) {
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

static void generate_command_define(GenerationContext *ctx, XML_Token command) {
    XML_Token *proto = find_next(command, "proto", NULL);
    assert(proto);

    XML_Token *command_name = find_next(*proto, "name", NULL);
    assert(command_name);

    sb_puts("#define ", &ctx->command_defines);

    if (ctx->use_snake_case) {
        assert(command_name->type == XML_TOKEN_NODE);
        assert(command_name->value.content.length == 1);

        XML_Token child = command_name->value.content.tokens[0];
        assert(child.type == XML_TOKEN_TEXT);

        write_snake_case(&ctx->command_defines, child.value.text);
    } else {
        write_inner_text(&ctx->command_defines, *command_name, -1);
    }

    sb_putc(' ', &ctx->command_defines);
    sb_puts(PREFIX, &ctx->command_defines);
    write_inner_text(&ctx->command_defines, *command_name, -1);
    sb_putc('\n', &ctx->command_defines);
}

static XML_StringView get_command_name(XML_Token command) {
    XML_Token *proto = find_next(command, "proto", NULL);
    assert(proto);
    XML_Token *name = find_next(*proto, "name", NULL);
    assert(name);

    assert(name->value.content.length == 1);
    assert(name->value.content.tokens[0].type == XML_TOKEN_TEXT);
    return name->value.content.tokens[0].value.text;
}

void generate_command(GenerationContext *ctx, XML_Token commands,
                      XML_StringView name) {
    size_t cmd_index = 0;
    XML_Token *command = NULL;

    while ((command = find_next(commands, "command", &cmd_index))) {
        if (XML_str_eq(get_command_name(*command), name)) {
            generate_command_wrapper(ctx, *command);
            generate_command_declaration(ctx, *command);
            generate_command_define(ctx, *command);
            return;
        }
    }
}

static void write_header(GenerationContext *ctx) {
    sb_puts("typedef void (*CladProc)(void);\n", &ctx->command_prototypes);
    sb_puts("typedef CladProc (CladProcAddrLoader)(const char *);\n\n",
            &ctx->command_prototypes);

    sb_puts("typedef struct {\n", &ctx->command_lookup);
    sb_puts("    CladProc proc;\n", &ctx->command_lookup);
    sb_puts("    const char *name;\n", &ctx->command_lookup);
    sb_puts("} Proc;\n\n", &ctx->command_lookup);
    sb_puts("static Proc lookup[] = {\n", &ctx->command_lookup);
}

static void write_footer(GenerationContext *ctx) {
    sb_puts("};\n\n", &ctx->command_lookup);

    // Generate initialization function
    sb_puts("int clad_init_gl(CladProcAddrLoader load_proc)\n",
            &ctx->command_lookup);
    sb_puts("{\n", &ctx->command_lookup);
    sb_puts(
        "    for (size_t i = 0; i < sizeof(lookup) / sizeof(*lookup); i++)\n",
        &ctx->command_lookup);
    sb_puts("    {\n", &ctx->command_lookup);
    sb_puts("        lookup[i].proc = load_proc(lookup[i].name);\n",
            &ctx->command_lookup);
    sb_puts("        if (lookup[i].proc == NULL)\n", &ctx->command_lookup);
    sb_puts("        {\n", &ctx->command_lookup);
    sb_puts("            return 0;\n", &ctx->command_lookup);
    sb_puts("        }\n", &ctx->command_lookup);
    sb_puts("    }\n", &ctx->command_lookup);
    sb_puts("    return 1;\n", &ctx->command_lookup);
    sb_puts("}\n\n", &ctx->command_lookup);

    // Also generate it in the header
    sb_puts("int clad_init_gl(CladProcAddrLoader load_proc);\n",
            &ctx->command_prototypes);
}

static bool is_version_leq(XML_Token feature, GLAPIType expected_api,
                           GLVersion max_version) {
    XML_StringView api;
    if (!XML_get_attribute(feature, "api", &api)) {
        fprintf(stderr,
                "Generation error: expected attribute `api` on <feature>!\n");
        return false;
    }

    if (gl_api_from_sv(api) != expected_api)
        return false;

    XML_StringView version;
    if (!XML_get_attribute(feature, "name", &version)) {
        fprintf(stderr,
                "Generation error: expected attribute `name` on <feature>!\n");
        return false;
    }

    if (gl_version_from_sv(version) > max_version)
        return false;

    return true;
}

static void register_require(GenerationContext *ctx, XML_Token parent,
                             bool require) {
    for (size_t i = 0; i < parent.value.content.length; i++) {
        XML_Token def = parent.value.content.tokens[i];

        if (def.type != XML_TOKEN_NODE) {
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
        if (!XML_get_attribute(def, "name", &name)) {
            fprintf(stderr, "Generation error: expected `name` attribute!\n");
            continue;
        }

        rl_add(&ctx->requirements, def_type, name, require);
    }
}

static void gather_featureset(GenerationContext *ctx, XML_Token root) {
    size_t version_tag_index = 0;
    for (GLVersion version = GL_VERSION_1_0; version <= ctx->version;
         version++) {
        XML_Token *feature_tag = find_next(root, "feature", &version_tag_index);

        // There are no more <feature> tags in the file.
        if (!feature_tag)
            break;

        if (!is_version_leq(*feature_tag, ctx->api, ctx->version))
            continue;

        // This is a bit cursed, but if it works...
        for (size_t r_index = 0;;) {
            bool require = true;
            XML_Token *r = find_next(*feature_tag, "require", &r_index);
            if (!r) {
                r = find_next(*feature_tag, "remove", &r_index);
                require = false;
            }

            // Neither <require> nor <remove> could be found, exit the loop.
            if (!r) {
                break;
            }

            XML_StringView profile;
            if (XML_get_attribute(*r, "profile", &profile)) {
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

static void write_output_header(GenerationContext ctx) {
    fputs("#ifndef CLAD_H\n", ctx.output_header);
    fputs("#define CLAD_H\n", ctx.output_header);

    fwrite(ctx.types.ptr, 1, ctx.types.length, ctx.output_header);
    fputc('\n', ctx.output_header);
    fwrite(ctx.enums.ptr, 1, ctx.enums.length, ctx.output_header);
    fputc('\n', ctx.output_header);
    fwrite(ctx.command_defines.ptr, 1, ctx.command_defines.length,
           ctx.output_header);
    fputc('\n', ctx.output_header);
    fwrite(ctx.command_prototypes.ptr, 1, ctx.command_prototypes.length,
           ctx.output_header);
    fputc('\n', ctx.output_header);

    fputs("#endif\n", ctx.output_header);
}

static void write_output_source(GenerationContext ctx) {
    fprintf(ctx.output_source, "#include <clad/gl.h>\n");
    fprintf(ctx.output_source, "#include <stdlib.h>\n\n");
    fwrite(ctx.command_lookup.ptr, 1, ctx.command_lookup.length,
           ctx.output_source);
    fwrite(ctx.command_wrappers.ptr, 1, ctx.command_wrappers.length,
           ctx.output_source);
}

static XML_StringView get_enum_name(XML_Token _enum) {
    XML_StringView name;
    bool found_name = XML_get_attribute(_enum, "name", &name);
    assert(found_name);
    return name;
}

static XML_StringView get_enum_value(XML_Token _enum) {
    XML_StringView value;
    bool found_value = XML_get_attribute(_enum, "value", &value);
    assert(found_value);
    return value;
}

static void generate_enum(GenerationContext *ctx, XML_Token root,
                          XML_StringView name) {
    size_t enums_index = 0;
    XML_Token *enums = NULL;

    while ((enums = find_next(root, "enums", &enums_index))) {
        size_t enum_index = 0;
        XML_Token *_enum = NULL;

        while ((_enum = find_next(*enums, "enum", &enum_index))) {
            XML_StringView enum_name = get_enum_name(*_enum);

            if (!XML_str_eq(enum_name, name)) {
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

static void generate(XML_Token root, CladArguments args, FILE *output_header,
                     FILE *output_source) {
    assert(root.type == XML_TOKEN_NODE);

    GenerationContext ctx =
        init_context(args.use_snake_case, args.api, args.profile, args.version,
                     output_header, output_source);
    generate_types(&ctx, root);
    gather_featureset(&ctx, root);
    write_header(&ctx);

    XML_Token *commands = find_next(root, "commands", NULL);
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

    write_footer(&ctx);
    write_output_header(ctx);
    write_output_source(ctx);
    free_context(ctx);
}

char *shift_arguments(char ***argv) {
    char *next_string = **argv;
    if (next_string != NULL) {
        (*argv)++;
    }
    return next_string;
}

bool streq(const char *a, const char *b) {
    size_t i = 0;

    while (a[i] == b[i]) {
        if (a[i] == '\0') {
            return true;
        }
        i++;
    }

    return false;
}

static bool parse_kv(char ***argv, char **value, const char *current_arg,
                     const char *key, const char *value_kind) {
    if (streq(current_arg, key)) {
        *value = shift_arguments(argv);
        if (*value == NULL) {
            fprintf(stderr, "error: expected %s after %s!\n", value_kind, key);
        }
        return true;
    }
    return false;
}

static XML_StringView sv_from_cstr(const char *str) {
    XML_StringView sv;
    sv.start = str;
    sv.length = XML_strlen(str);
    return sv;
}

static CladArguments parse_commandline_arguments(char **args) {
    CladArguments parsed_arguments = { 0 };

    char **argv = args;
    char *value = NULL;
    int has_been_parsed = 0;
    const char *next_argument = NULL;

    // Skip first argument.
    shift_arguments(&argv);

    while ((next_argument = shift_arguments(&argv))) {
        if (streq(next_argument, "\\")) {
            continue;
        }
        if (streq(next_argument, "--snake-case")) {
            parsed_arguments.use_snake_case = true;
        } else if (parse_kv(&argv, &value, next_argument, "--in-xml",
                            "file path")) {
            if (!value)
                break;
            parsed_arguments.input_xml = value;
            has_been_parsed |= 0x01;
        } else if (parse_kv(&argv, &value, next_argument, "--out-header",
                            "file path")) {
            if (!value)
                break;
            parsed_arguments.output_header = value;
            has_been_parsed |= 0x02;
        } else if (parse_kv(&argv, &value, next_argument, "--out-source",
                            "file path")) {
            if (!value)
                break;
            parsed_arguments.output_source = value;
            has_been_parsed |= 0x04;
        } else if (parse_kv(&argv, &value, next_argument, "--api", "api")) {
            if (!value)
                break;
            GLAPIType api = gl_api_from_sv(sv_from_cstr(value));
            if (api == GL_API_INVALID) {
                fprintf(stderr, "error: failed to parse API version: %s\n",
                        value);
                break;
            }
            parsed_arguments.api = api;
            has_been_parsed |= 0x08;
        } else if (parse_kv(&argv, &value, next_argument, "--profile",
                            "profile")) {
            if (!value)
                break;
            GLProfile profile = gl_profile_from_sv(sv_from_cstr(value));
            if (profile == GL_PROFILE_INVALID) {
                fprintf(stderr, "error: failed to parse profile: %s\n", value);
                break;
            }
            parsed_arguments.profile = profile;
            has_been_parsed |= 0x10;
        } else if (parse_kv(&argv, &value, next_argument, "--version",
                            "version")) {
            if (!value)
                break;
            GLVersion version = gl_version_from_sv_short(sv_from_cstr(value));
            if (version == GL_VERSION_INVALID) {
                fprintf(stderr, "error: failed to parse version: %s\n", value);
                break;
            }
            parsed_arguments.version = version;
            has_been_parsed |= 0x20;
        }
    }

    parsed_arguments.parsed_succesfully = has_been_parsed == 0x3f;
    return parsed_arguments;
}

static FILE *try_to_open(const char *path, const char *mode) {
    FILE *fp = fopen(path, mode);

    if (!fp) {
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

    CladArguments arguments = parse_commandline_arguments(argv);
    if (!arguments.parsed_succesfully) {
        fprintf(stderr, "Usage: clad "
                        "--in-xml <file> "
                        "--out-header <file> "
                        "--out-source <file> "
                        "--api <openl api> "
                        "--profile <opengl profile> "
                        "--version <opengl version> "
                        "[--snake-case]\n");
        return ret;
    }

    FILE *output_header = try_to_open(arguments.output_header, "w");
    FILE *output_source = try_to_open(arguments.output_source, "w");

    if (output_header && output_source) {
        XML_Token root;
        char *src = XML_read_file(arguments.input_xml);
        if (!src)
            goto failure;

        if (XML_parse_file(src, &root)) {
            generate(root, arguments, output_header, output_source);
            XML_free(root);
        }

        free(src);
        ret = EXIT_SUCCESS;
    }

failure:
    try_to_close(output_header);
    try_to_close(output_source);
    return ret;
}
