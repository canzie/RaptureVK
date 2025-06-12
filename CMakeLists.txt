cmake_minimum_required(VERSION 3.11)
project(Rapture VERSION 1.0.0 LANGUAGES C CXX)

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

# Add subdirectories
add_subdirectory(Engine)
add_subdirectory(Editor) 