set(ENGINE_NAME RaptureVK)

# Include vendor libraries
include(${CMAKE_CURRENT_SOURCE_DIR}/vendor/vendor_libraries.cmake)

# Collect source files
file(GLOB_RECURSE ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

# Create engine library
add_library(${ENGINE_NAME} STATIC ${ENGINE_SOURCES})

rapture_set_warnings(${ENGINE_NAME})

# Include directories
target_include_directories(${ENGINE_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Vulkan clips at 0 <= z <= w, so glm must build projections for that range. PUBLIC because a
# matrix built in one target and consumed in another has to agree on the convention, and the same
# holds for RAPTURE_DEBUG wherever it changes what a header declares.
target_compile_definitions(${ENGINE_NAME} PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    $<$<OR:$<CONFIG:Debug>,$<BOOL:${RAPTURE_DEBUG}>>:RAPTURE_DEBUG>
)

target_link_libraries(${ENGINE_NAME}
    PUBLIC
        engine_public_dependencies
        Threads::Threads
    PRIVATE
        engine_private_dependencies
)

# Set output directory
set_target_properties(${ENGINE_NAME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
