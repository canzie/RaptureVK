#!/bin/bash

# Script to set up compile_commands.json for clangd LSP support
# Run this after building your project

BUILD_DIR="build"
COMPILE_COMMANDS_FILE="compile_commands.json"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory '$BUILD_DIR' not found. Please build the project first."
    exit 1
fi

# Check if compile_commands.json exists in build directory
if [ ! -f "$BUILD_DIR/$COMPILE_COMMANDS_FILE" ]; then
    echo "compile_commands.json not found in '$BUILD_DIR'. Make sure CMAKE_EXPORT_COMPILE_COMMANDS is enabled."
    exit 1
fi

# Copy compile_commands.json to project root
cp "$BUILD_DIR/$COMPILE_COMMANDS_FILE" .

echo "Successfully copied compile_commands.json to project root."
echo "Your clangd LSP should now have access to the compilation database." 