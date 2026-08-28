file(GLOB_RECURSE ENGINE_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp
)

rapture_add_tests(RaptureEngineTests
    SOURCES ${ENGINE_TEST_SOURCES}
    LIBRARIES RaptureVK
)
