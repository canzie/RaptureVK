file(GLOB ENGINE_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp
)

rapture_add_tests(RaptureEngineTests
    SOURCES ${ENGINE_TEST_SOURCES}
    LIBRARIES RaptureVK
)

# Tests that bring up a headless application, so they need a Vulkan device
file(GLOB ENGINE_GPU_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/gpu/*_test.cpp
)

rapture_add_tests(RaptureEngineGpuTests
    SOURCES ${ENGINE_GPU_TEST_SOURCES}
    LIBRARIES RaptureVK stb_image
)
