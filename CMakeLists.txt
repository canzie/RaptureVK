cmake_minimum_required(VERSION 3.11)
project(Rapture VERSION 1.0.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type specified, defaulting to Debug")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Include FetchContent module for downloading dependencies
include(FetchContent)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable parallel compilation
if(MSVC)
    add_compile_options(/MP)
else()
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
        set(CMAKE_BUILD_PARALLEL_LEVEL ${N})
    endif()
endif()

# Set MSVC diagnostic options for clearer error messages
if(MSVC)
    add_compile_options(
        /FC     # Display full path of source code in diagnostics
        /diagnostics:caret # Show column and caret location in errors
    )
    
    # Release mode optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ob2 /Oi /Ot /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG")
else()
    # GCC/Clang optimizations for Release mode
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -flto=auto")
endif()

# Optional sanitizer build (none, address, thread). Use thread to chase data races,
# address to chase heap corruption. cmake -DRAPTURE_SANITIZER=thread ..
set(RAPTURE_SANITIZER "none" CACHE STRING "Sanitizer to build with: none, address, thread")
if(NOT MSVC AND NOT RAPTURE_SANITIZER STREQUAL "none")
    message(STATUS "Building with ${RAPTURE_SANITIZER} sanitizer")
    add_compile_options(-fsanitize=${RAPTURE_SANITIZER} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${RAPTURE_SANITIZER})
endif()

option(RAPTURE_BUILD_UTESTS "Build the unit test executables" OFF)
option(RAPTURE_EXEC_UTESTS "Run the unit tests as part of the build" OFF)
option(RAPTURE_UTESTS_VALGRIND "Run the unit tests under valgrind" OFF)

# Add subdirectories
add_subdirectory(Engine)
add_subdirectory(Editor)

if(RAPTURE_BUILD_UTESTS)
    enable_testing()
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/RaptureTests.cmake)
    add_subdirectory(Engine/test)
    add_subdirectory(Editor/test)
endif()