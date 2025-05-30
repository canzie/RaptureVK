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
elseif(MSVC)
    # For MSVC, check if we're in debug configuration
    add_compile_definitions($<$<CONFIG:Debug>:RAPTURE_DEBUG>)
    add_compile_definitions($<$<CONFIG:Debug>:_DEBUG>)
    
    message(STATUS "MSVC build - Debug flags will be enabled in Debug configuration only")
else()
    message(STATUS "Release build - Debug flags disabled")
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