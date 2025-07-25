add_executable(clad_generator
    clad.c
    xml.c
    string_buffer.c
)

if(NOT CLAD_GL_API)
    set(CLAD_GL_API "gl")
endif()

if(NOT CLAD_GL_PROFILE)
    set(CLAD_GL_PROFILE "core")
endif()

if(NOT CLAD_GL_VERSION)
    set(CLAD_GL_VERSION "3.3")
endif()

option(CLAD_USE_SNAKE_CASE "Use snake_case in favour of camelCase" OFF)
if(${CLAD_USE_SNAKE_CASE})
    set(CLAD_SNAKE_CASE --snake-case)
else()
    set(CLAD_SNAKE_CASE "")
endif()

set(GENERATED_HEADER ${PROJECT_BINARY_DIR}/generated/include/clad/gl.h)
set(GENERATED_SOURCE ${PROJECT_BINARY_DIR}/generated/gl.c)

cmake_path(GET GENERATED_HEADER PARENT_PATH GENERATED_HEADER_DIR)
cmake_path(GET GENERATED_SOURCE PARENT_PATH GENERATED_SOURCE_DIR)
cmake_path(GET GENERATED_HEADER_DIR PARENT_PATH GENERATED_INCLUDE_DIR)

add_custom_command(
    OUTPUT ${GENERATED_HEADER} ${GENERATED_SOURCE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_HEADER_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_SOURCE_DIR}
    COMMAND clad_generator
        --in-xml ${PROJECT_SOURCE_DIR}/files/gl.xml
        --out-header ${GENERATED_HEADER}
        --out-source ${GENERATED_SOURCE}
        --api ${CLAD_GL_API}
        --profile ${CLAD_GL_PROFILE}
        --version ${CLAD_GL_VERSION}
        ${CLAD_SNAKE_CASE}
    DEPENDS clad_generator
)

add_custom_command(
    OUTPUT ${GENERATED_INCLUDE_DIR}/KHR/khrplatform.h
    COMMAND ${CMAKE_COMMAND} -E copy
        ${PROJECT_SOURCE_DIR}/files/khrplatform.h
        ${GENERATED_INCLUDE_DIR}/KHR/khrplatform.h
)

add_custom_target(code_generation
    DEPENDS
        ${GENERATED_HEADER}
        ${GENERATED_SOURCE}
        ${GENERATED_INCLUDE_DIR}/KHR/khrplatform.h
)

add_library(clad STATIC ${GENERATED_SOURCE})
add_dependencies(clad code_generation)
target_include_directories(clad PRIVATE ${GENERATED_INCLUDE_DIR})
