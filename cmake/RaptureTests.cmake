# Shared unit test machinery: one GoogleTest fetch, and a helper each test directory calls to
# declare its own executable.

include(FetchContent)
include(GoogleTest)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(googletest)

# rapture_add_tests(<target> SOURCES ... [LIBRARIES ...] [INCLUDES ...])
#
# TODO: one executable per test file, once the engine is a shared library and each of them
# links against it rather than pulling in a copy of the static one.
function(rapture_add_tests TARGET)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES;INCLUDES" ${ARGN})

    add_executable(${TARGET} ${ARG_SOURCES})
    target_link_libraries(${TARGET} PRIVATE ${ARG_LIBRARIES} GTest::gtest_main)

    if(ARG_INCLUDES)
        target_include_directories(${TARGET} PRIVATE ${ARG_INCLUDES})
    endif()

    set_target_properties(${TARGET} PROPERTIES
        CXX_STANDARD ${CMAKE_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    set(RUN_COMMAND $<TARGET_FILE:${TARGET}>)

    if(RAPTURE_UTESTS_VALGRIND)
        find_program(VALGRIND_EXECUTABLE valgrind)
        if(VALGRIND_EXECUTABLE)
            set(RUN_COMMAND
                ${VALGRIND_EXECUTABLE}
                --leak-check=full
                --track-origins=yes
                --error-exitcode=1
                ${RUN_COMMAND}
            )
        else()
            message(WARNING "RAPTURE_UTESTS_VALGRIND is on but valgrind was not found, running ${TARGET} directly")
        endif()
    endif()

    if(RAPTURE_UTESTS_VALGRIND)
        add_test(NAME ${TARGET} COMMAND ${RUN_COMMAND})
    else()
        gtest_discover_tests(${TARGET})
    endif()

    if(RAPTURE_EXEC_UTESTS)
        # a custom target rather than a POST_BUILD command, which only fires when the executable relinks
        add_custom_target(Run${TARGET} ALL
            COMMAND ${RUN_COMMAND}
            DEPENDS ${TARGET}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running ${TARGET}"
            VERBATIM
        )
    endif()
endfunction()
