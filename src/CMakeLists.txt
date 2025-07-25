add_executable(clad_generator
    clad.c
    xml.c
    string_buffer.c
)

# add_custom_command(
#     OUTPUT gl.c gl.h
#     COMMAND ${CMAKE_COMMAND} -E echo "hello, world!"
#     DEPENDS clad_generator
#     COMMENT "Runs the generator to create a source and header file."
# )

set(GL_API gl)
set(GL_PROFILE core)
set(GL_VERSION 3.3)

# Usage: clad --in-xml <file> --out-header <file> --out-source <file> --api <openl api> --profile <opengl profile> --version <opengl version>

add_custom_target(generate_files ALL
    COMMAND clad_generator
        --in-xml ${PROJECT_SOURCE_DIR}/files/gl.xml
        --out-header gl.h
        --out-source gl.c
        --api ${GL_API}
        --profile ${GL_PROFILE}
        --version ${GL_VERSION}
    DEPENDS clad_generator
    BYPRODUCTS gl.c gl.h
)
