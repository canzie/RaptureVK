file(GLOB_RECURSE EDITOR_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp
)

# The editor is an executable, so a test compiles the units it covers rather than linking it.
# Splitting the editor into a library plus a thin main would let this be a glob like the engine's.
set(EDITOR_TEST_UNITS
    ${CMAKE_SOURCE_DIR}/Editor/src/text/TextBuffer.cpp
)

rapture_add_tests(RaptureEditorTests
    SOURCES ${EDITOR_TEST_SOURCES} ${EDITOR_TEST_UNITS}
    LIBRARIES RaptureVK
    INCLUDES
        ${CMAKE_SOURCE_DIR}/Editor/src
        ${CMAKE_SOURCE_DIR}/Engine/vendor/Amethyst/libamethyst/src
)
