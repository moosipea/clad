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

add_custom_target(generate_files ALL
    COMMAND clad_generator
    DEPENDS clad_generator
    BYPRODUCTS gl.c gl.h
)
