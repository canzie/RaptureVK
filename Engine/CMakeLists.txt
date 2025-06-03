set(ENGINE_NAME RaptureVK)

# Include vendor libraries
include(${CMAKE_CURRENT_SOURCE_DIR}/vendor/vendor_libraries.cmake)

# Collect source files
file(GLOB_RECURSE ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)



# Debug mode settings - check in multiple ways
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR RAPTURE_DEBUG)
    message(STATUS "Debug build detected - Enabling debug flags")
    add_definitions(-DRAPTURE_DEBUG)
    add_definitions(-D_DEBUG)
    add_definitions(-DRAPTURE_TRACY_PROFILING_ENABLED=1)
    add_definitions(-DTRACY_ENABLE)
    add_definitions(-DTRACY_MEMORY)
    if(MSVC)
        target_compile_options(${ENGINE_NAME} PRIVATE /W4 /WX)
        add_compile_definitions($<$<CONFIG:Debug>:RAPTURE_DEBUG>)
        add_compile_definitions($<$<CONFIG:Debug>:_DEBUG>)
        message(STATUS "MSVC Debug build - Enabling debug flags")
    else()
        target_compile_options(${ENGINE_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
        message(STATUS "GCC Debug build - Enabling debug flags")
    endif()
else()
    message(STATUS "Release build - Debug flags disabled")
    #add_definitions(-DNDEBUG)
endif()

# Create engine library
add_library(${ENGINE_NAME} STATIC ${ENGINE_SOURCES})

# Create engine executable (changed from library)
#add_executable(${ENGINE_NAME} ${ENGINE_SOURCES})

# Include directories
target_include_directories(${ENGINE_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link with vendor libraries
target_link_libraries(${ENGINE_NAME} PUBLIC
    vendor_libraries
    Threads::Threads
)

# Add additional compile definitions
target_compile_definitions(${ENGINE_NAME} PRIVATE
    SPDLOG_COMPILED_LIB
    FMT_HEADER_ONLY=0
)

# Set output directory
set_target_properties(${ENGINE_NAME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
) 