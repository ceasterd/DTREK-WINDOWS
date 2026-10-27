cmake_minimum_required(VERSION 3.20)

project(dtrek_core VERSION 0.5.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(DTREK_ARTIFACT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../artifacts/native"
    CACHE PATH "Directory for the built native DLL and test executable")

add_library(dtrek_core SHARED
    src/core.c
    src/math.c
    src/rng.c
    src/robot.c
)

target_include_directories(dtrek_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories(dtrek_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_definitions(dtrek_core PRIVATE DTREK_CORE_BUILD)

if(MSVC)
    target_compile_options(dtrek_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(dtrek_core PRIVATE -Wall -Wextra -Wpedantic)
    if(UNIX AND NOT APPLE)
        target_link_libraries(dtrek_core PRIVATE m)
    endif()
endif()

set_target_properties(dtrek_core PROPERTIES
    OUTPUT_NAME dtrek_core
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${DTREK_ARTIFACT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${DTREK_ARTIFACT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${DTREK_ARTIFACT_DIR}"
    PDB_OUTPUT_DIRECTORY "${DTREK_ARTIFACT_DIR}"
)

foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(dtrek_core PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${DTREK_ARTIFACT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_${config} "${DTREK_ARTIFACT_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_${config} "${DTREK_ARTIFACT_DIR}"
        PDB_OUTPUT_DIRECTORY_${config} "${DTREK_ARTIFACT_DIR}"
    )
endforeach()

option(DTREK_BUILD_TESTS "Build native core tests" ON)

if(DTREK_BUILD_TESTS)
    enable_testing()

    add_executable(dtrek_core_tests tests/test_core.c)
    target_link_libraries(dtrek_core_tests PRIVATE dtrek_core)
    target_include_directories(dtrek_core_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )

    if(MSVC)
        target_compile_options(dtrek_core_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(dtrek_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    set_target_properties(dtrek_core_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${DTREK_ARTIFACT_DIR}"
        PDB_OUTPUT_DIRECTORY "${DTREK_ARTIFACT_DIR}"
    )
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(dtrek_core_tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config} "${DTREK_ARTIFACT_DIR}"
            PDB_OUTPUT_DIRECTORY_${config} "${DTREK_ARTIFACT_DIR}"
        )
    endforeach()

    add_test(NAME dtrek_core_tests COMMAND dtrek_core_tests
        WORKING_DIRECTORY "${DTREK_ARTIFACT_DIR}")
endif()
