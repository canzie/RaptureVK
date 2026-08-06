# Editor CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Set the Editor project name
set(EDITOR_NAME RaptureEditor)

# Find all source files in the Editor directory
file(GLOB_RECURSE EDITOR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

# Create Editor executable with appropriate entry point
add_executable(${EDITOR_NAME} ${EDITOR_SOURCES})

# Set the desired output executable name (can contain spaces)
set_target_properties(${EDITOR_NAME} PROPERTIES OUTPUT_NAME "RaptureEditor")

# Set include directories
target_include_directories(${EDITOR_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/Engine/src  # Include Engine headers
)

# Ensure C++20 standard
set_property(TARGET ${EDITOR_NAME} PROPERTY CXX_STANDARD 20)
set_property(TARGET ${EDITOR_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)

# Link with Engine library and vendor libraries directly
target_link_libraries(${EDITOR_NAME} PRIVATE 
    RaptureVK
)

# Set output directory
set_target_properties(${EDITOR_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Fix MSVC warning about conflicting libraries
if(MSVC)
    # Avoid default library conflicts
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_target_properties(${EDITOR_NAME} PROPERTIES LINK_FLAGS "/NODEFAULTLIB:MSVCRT")
    endif()
endif()



# Assets are resolved relative to the executable at runtime. A development build links them back to
# the source tree so edits apply without rebuilding; an install copies the same layout instead.
set(EDITOR_ASSET_DIR $<TARGET_FILE_DIR:${EDITOR_NAME}>/assets)

add_custom_command(TARGET ${EDITOR_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${EDITOR_ASSET_DIR}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${EDITOR_ASSET_DIR}/engine
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_SOURCE_DIR}/Engine/assets ${EDITOR_ASSET_DIR}/engine
    COMMAND ${CMAKE_COMMAND} -E rm -f ${EDITOR_ASSET_DIR}/amethyst
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_SOURCE_DIR}/Engine/vendor/Amethyst/backends/shaders ${EDITOR_ASSET_DIR}/amethyst
    COMMENT "Linking engine assets next to the editor"
    VERBATIM
)

foreach(ASSET_SUBDIR fonts themes models textures)
    add_custom_command(TARGET ${EDITOR_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f ${EDITOR_ASSET_DIR}/${ASSET_SUBDIR}
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_SOURCE_DIR}/assets/${ASSET_SUBDIR} ${EDITOR_ASSET_DIR}/${ASSET_SUBDIR}
        VERBATIM
    )
endforeach()

# IDE source organization
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${EDITOR_SOURCES}) 
