set(UTESTS_NAME RaptureTests)

# --- GoogleTest ---
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

file(GLOB_RECURSE UTESTS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp
)

# TODO: one executable per test file, once the engine is a shared library and each of them
# links against it rather than pulling in a copy of the static one.
add_executable(${UTESTS_NAME} ${UTESTS_SOURCES})

target_link_libraries(${UTESTS_NAME} PRIVATE RaptureVK GTest::gtest_main)

set_target_properties(${UTESTS_NAME} PROPERTIES
    CXX_STANDARD ${CMAKE_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set(UTESTS_COMMAND $<TARGET_FILE:${UTESTS_NAME}>)

if(RAPTURE_UTESTS_VALGRIND)
    find_program(VALGRIND_EXECUTABLE valgrind)
    if(VALGRIND_EXECUTABLE)
        set(UTESTS_COMMAND
            ${VALGRIND_EXECUTABLE}
            --leak-check=full
            --track-origins=yes
            --error-exitcode=1
            ${UTESTS_COMMAND}
        )
    else()
        message(WARNING "RAPTURE_UTESTS_VALGRIND is on but valgrind was not found, running the tests directly")
    endif()
endif()

if(RAPTURE_UTESTS_VALGRIND)
    add_test(NAME ${UTESTS_NAME} COMMAND ${UTESTS_COMMAND})
else()
    include(GoogleTest)
    gtest_discover_tests(${UTESTS_NAME})
endif()

if(RAPTURE_EXEC_UTESTS)
    # a custom target rather than a POST_BUILD command, which only fires when the executable relinks
    add_custom_target(RunRaptureTests ALL
        COMMAND ${UTESTS_COMMAND}
        DEPENDS ${UTESTS_NAME}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running unit tests"
        VERBATIM
    )
endif()
