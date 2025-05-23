#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# Helper function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Ensure the script is run from the project root (where Engine directory exists)
if [ ! -d "Engine" ]; then
    echo "ERROR: This script must be run from the project root directory (e.g., RaptureVK)."
    exit 1
fi
echo "Script is running from the project root."

# --- Prerequisites Check ---
echo "Ensuring prerequisites (git, curl/wget, unzip) are available..."
if ! command_exists git; then echo "ERROR: git not found. Please install it and ensure it's in your PATH."; exit 1; fi
if ! (command_exists curl || command_exists wget); then echo "ERROR: curl or wget not found. Please install one and ensure it's in your PATH."; exit 1; fi
if ! command_exists unzip; then echo "ERROR: unzip not found. Please install it and ensure it's in your PATH."; exit 1; fi
if ! command_exists tar; then echo "ERROR: tar not found. Please install it and ensure it's in your PATH."; exit 1; fi # tar is used by .bat, good for consistency if zips are .tar.gz sometimes, though current are .zip
echo "All prerequisites found."

# --- Configuration ---
VENDOR_DIR_NAME="Engine/vendor" # Relative to project root

GLFW_VERSION="3.4"
GLM_VERSION="1.0.1"            # Tag version for GLM
ENTT_VERSION_TAG="v3.13.0"     # Full tag for EnTT
SPDLOG_VERSION_TAG="v1.14.1"   # Full tag for spdlog
STB_IMAGE_VERSION="master"     # Or a specific commit/tag
VMA_VERSION_TAG="v3.3.0"       # Full tag for Vulkan Memory Allocator
SPIRV_REFLECT_VERSION_TAG="main"    # Main branch for SPIRV-Reflect

GLFW_URL="https://github.com/glfw/glfw/releases/download/${GLFW_VERSION}/glfw-${GLFW_VERSION}.zip"
GLM_URL="https://github.com/g-truc/glm/archive/refs/tags/${GLM_VERSION}.zip" # Changed to archive zip
IMGUI_REPO="https://github.com/ocornut/imgui.git"
IMGUI_BRANCH="docking"
ENTT_URL="https://github.com/skypjack/entt/archive/refs/tags/${ENTT_VERSION_TAG}.zip"
SPDLOG_URL="https://github.com/gabime/spdlog/archive/refs/tags/${SPDLOG_VERSION_TAG}.zip"
STB_IMAGE_H_URL="https://raw.githubusercontent.com/nothings/stb/${STB_IMAGE_VERSION}/stb_image.h"
VMA_URL="https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/${VMA_VERSION_TAG}.zip"
SPIRV_REFLECT_URL="https://github.com/KhronosGroup/SPIRV-Reflect/archive/refs/heads/${SPIRV_REFLECT_VERSION_TAG}.zip"

# Function to download files (tries curl, then wget)
download() {
    local url="$1"
    local output_file="$2"
    echo "Downloading ${url} to ${output_file}..."
    if command_exists curl; then
        curl -L "${url}" -o "${output_file}"
    elif command_exists wget; then
        wget --quiet -O "${output_file}" "${url}" # Added --quiet
    else
        echo "ERROR: Neither curl nor wget is available. This should have been caught by prerequisite check."
        exit 1
    fi
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to download ${url}"
        rm -f "${output_file}" # Clean up partial download
        exit 1
    fi
    echo "Download complete."
}

# --- Display Summary and Ask for Confirmation ---
echo
echo "The following libraries will be downloaded/set up:"
echo "================================================================================"
printf "%-13s %-19s %s\n" "Library" "Version/Branch" "Source"
echo "--------------------------------------------------------------------------------"
printf "%-13s %-19s %s\n" "GLFW" "${GLFW_VERSION}" "${GLFW_URL}"
printf "%-13s %-19s %s\n" "GLM" "${GLM_VERSION}" "${GLM_URL}"
printf "%-13s %-19s %s\n" "ImGui" "${IMGUI_BRANCH} (branch)" "${IMGUI_REPO}"
printf "%-13s %-19s %s\n" "EnTT" "${ENTT_VERSION_TAG}" "${ENTT_URL}"
printf "%-13s %-19s %s\n" "spdlog" "${SPDLOG_VERSION_TAG}" "${SPDLOG_URL}"
printf "%-13s %-19s %s\n" "stb_image" "${STB_IMAGE_VERSION} (tag/commit)" "${STB_IMAGE_H_URL}"
printf "%-13s %-19s %s\n" "VMA" "${VMA_VERSION_TAG}" "${VMA_URL}"
printf "%-13s %-19s %s\n" "SPIRV-Reflect" "${SPIRV_REFLECT_VERSION_TAG}" "${SPIRV_REFLECT_URL}"
echo "================================================================================"
ABSOLUTE_VENDOR_DIR="$(cd "$(dirname "$0")" && pwd)/${VENDOR_DIR_NAME}" # Get absolute path robustly
echo "All libraries will be placed in: ${ABSOLUTE_VENDOR_DIR}"
echo

read -r -p "Proceed with setup? (Y/N): " confirmation
if [[ ! "$confirmation" =~ ^[Yy]$ ]]; then
    echo "Setup cancelled by user."
    exit 0
fi
echo "Starting library setup..."

# --- Create Vendor Directory ---
echo "Creating vendor directory: ${VENDOR_DIR_NAME}"
mkdir -p "${VENDOR_DIR_NAME}"
if [ ! -d "${VENDOR_DIR_NAME}" ]; then
    echo "ERROR: Failed to create directory ${VENDOR_DIR_NAME}. Check permissions."
    exit 1
fi

# --- Change to Vendor Directory ---
echo "Changing to vendor directory: ${VENDOR_DIR_NAME}"
cd "${VENDOR_DIR_NAME}"
if [ "$PWD" != "$ABSOLUTE_VENDOR_DIR" ]; then
    echo "ERROR: Failed to change directory to ${ABSOLUTE_VENDOR_DIR}."
    echo "Current directory is: $PWD"
    exit 1
fi
echo "Successfully changed into $PWD"


# --- Download and Setup Libraries (Actual commands) ---

# --- GLFW ---
echo
echo "Setting up GLFW ${GLFW_VERSION}..."
rm -rf GLFW glfw-${GLFW_VERSION} glfw.zip # Clean up old
download "${GLFW_URL}" "glfw.zip"
echo "Extracting GLFW..."
unzip -q glfw.zip -d . # Extract here
if [ $? -ne 0 ]; then echo "ERROR: Failed to extract GLFW."; rm -f glfw.zip; exit 1; fi
mv "glfw-${GLFW_VERSION}" "GLFW"
rm -f glfw.zip
echo "GLFW setup complete."

# --- GLM ---
echo
echo "Setting up GLM ${GLM_VERSION}..."
rm -rf glm glm-${GLM_VERSION} glm.zip # Clean up old
download "${GLM_URL}" "glm.zip"
echo "Extracting GLM..."
unzip -q glm.zip -d . # Extract here
if [ $? -ne 0 ]; then echo "ERROR: Failed to extract GLM."; rm -f glm.zip; exit 1; fi
# GLM from archive tag extracts to glm-${GLM_VERSION}/glm/
if [ -d "glm-${GLM_VERSION}/glm" ]; then
    mv "glm-${GLM_VERSION}/glm" "./glm"
    rm -rf "glm-${GLM_VERSION}"
else
    echo "ERROR: Expected directory structure glm-${GLM_VERSION}/glm not found after GLM extraction."
    exit 1
fi
rm -f glm.zip
echo "GLM setup complete."

# --- ImGui (docking branch) ---
echo
echo "Setting up ImGui (${IMGUI_BRANCH} branch)..."
echo "Removing existing ImGui directory if present..."
rm -rf imgui
echo "Cloning ImGui from ${IMGUI_REPO}..."
git clone --branch "${IMGUI_BRANCH}" "${IMGUI_REPO}" imgui --depth 1
if [ $? -ne 0 ]; then echo "ERROR: Failed to clone ImGui."; exit 1; fi
echo "ImGui setup complete."

# --- EnTT ---
ENTT_VERSION_NO_V=${ENTT_VERSION_TAG#v} # Remove 'v' prefix for directory name
echo
echo "Setting up EnTT ${ENTT_VERSION_TAG} (extracted dir expected: entt-${ENTT_VERSION_NO_V})..."
rm -rf entt "entt-${ENTT_VERSION_NO_V}" entt.zip # Clean up old
download "${ENTT_URL}" "entt.zip"
echo "Extracting EnTT..."
unzip -q entt.zip -d . # Extract here
if [ $? -ne 0 ]; then echo "ERROR: Failed to extract EnTT."; rm -f entt.zip; exit 1; fi

EXTRACTED_ENTT_DIR="entt-${ENTT_VERSION_NO_V}"
if [ ! -d "${EXTRACTED_ENTT_DIR}" ]; then
    echo "ERROR: Expected extracted directory "${EXTRACTED_ENTT_DIR}" not found."
    exit 1
fi

# Use single include header: entt-${ENTT_VERSION_NO_V}/single_include/entt/entt.hpp
# Target: entt/include/entt/entt.hpp
SINGLE_HEADER_PATH="${EXTRACTED_ENTT_DIR}/single_include/entt/entt.hpp"
TARGET_ENTT_DIR="entt/include/entt"

echo "Looking for EnTT single header at: ${SINGLE_HEADER_PATH}"
if [ ! -f "${SINGLE_HEADER_PATH}" ]; then
    echo "ERROR: EnTT single header not found at ${SINGLE_HEADER_PATH}."
    echo "Listing contents of ${EXTRACTED_ENTT_DIR}/single_include/entt/:"
    ls -A "${EXTRACTED_ENTT_DIR}/single_include/entt/" 2>/dev/null || echo "(directory not found or empty)"
    exit 1
fi

mkdir -p "${TARGET_ENTT_DIR}"
mv "${SINGLE_HEADER_PATH}" "${TARGET_ENTT_DIR}/entt.hpp"
if [ $? -ne 0 ]; then echo "ERROR: Failed to move EnTT single header."; exit 1; fi

rm -rf "${EXTRACTED_ENTT_DIR}"
rm -f entt.zip
echo "EnTT setup complete."

# --- spdlog ---
SPDLOG_VERSION_NO_V=${SPDLOG_VERSION_TAG#v} # Remove 'v' prefix for directory name
echo
echo "Setting up spdlog ${SPDLOG_VERSION_TAG} (extracted dir expected: spdlog-${SPDLOG_VERSION_NO_V})..."
rm -rf spdlog "spdlog-${SPDLOG_VERSION_NO_V}" spdlog.zip # Clean up old
download "${SPDLOG_URL}" "spdlog.zip"
echo "Extracting spdlog..."
unzip -q spdlog.zip -d . # Extract here
if [ $? -ne 0 ]; then echo "ERROR: Failed to extract spdlog."; rm -f spdlog.zip; exit 1; fi

EXTRACTED_SPDLOG_DIR="spdlog-${SPDLOG_VERSION_NO_V}"
if [ ! -d "${EXTRACTED_SPDLOG_DIR}" ]; then
    echo "ERROR: Expected extracted directory "${EXTRACTED_SPDLOG_DIR}" not found."
    exit 1
fi
mv "${EXTRACTED_SPDLOG_DIR}" "spdlog"
rm -f spdlog.zip
echo "spdlog setup complete."

# --- stb_image ---
echo
echo "Setting up stb_image (${STB_IMAGE_VERSION} tag/commit)..."
rm -rf stb_image # Clean up old
mkdir -p "stb_image"
if [ $? -ne 0 ]; then echo "ERROR: Failed to create stb_image directory."; exit 1; fi
download "${STB_IMAGE_H_URL}" "stb_image/stb_image.h"
echo "Creating stb_image.cpp..."
cat << EOF > "stb_image/stb_image.cpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
EOF
if [ $? -ne 0 ]; then echo "ERROR: Failed to create stb_image.cpp."; exit 1; fi
echo "stb_image setup complete."

# --- Vulkan Memory Allocator (VMA) ---
VMA_VERSION_NO_V=${VMA_VERSION_TAG#v} # Remove 'v' prefix for directory name
echo
echo "Setting up Vulkan Memory Allocator ${VMA_VERSION_TAG} (extracted dir expected: VulkanMemoryAllocator-${VMA_VERSION_NO_V})..."
rm -rf VulkanMemoryAllocator "VulkanMemoryAllocator-${VMA_VERSION_NO_V}" vma.zip # Clean up old
download "${VMA_URL}" "vma.zip"
echo "Extracting Vulkan Memory Allocator..."
unzip -q vma.zip -d . # Extract here
if [ $? -ne 0 ]; then echo "ERROR: Failed to extract Vulkan Memory Allocator."; rm -f vma.zip; exit 1; fi

EXTRACTED_VMA_DIR="VulkanMemoryAllocator-${VMA_VERSION_NO_V}"
if [ ! -d "${EXTRACTED_VMA_DIR}" ]; then
    echo "ERROR: Expected extracted directory "${EXTRACTED_VMA_DIR}" not found."
    exit 1
fi
mv "${EXTRACTED_VMA_DIR}" "VulkanMemoryAllocator"
rm -f vma.zip
echo "Vulkan Memory Allocator setup complete."

# --- SPIRV-Reflect ---
echo
echo "Setting up SPIRV-Reflect (${SPIRV_REFLECT_VERSION_TAG} branch)..."
rm -rf SPIRV-Reflect SPIRV-Reflect-${SPIRV_REFLECT_VERSION_TAG} spirv_reflect.zip # Clean up old
download "${SPIRV_REFLECT_URL}" "spirv_reflect.zip"
echo "Extracting SPIRV-Reflect..."
unzip -q spirv_reflect.zip -d . # Extract here
if [ $? -ne 0 ]; then echo "ERROR: Failed to extract SPIRV-Reflect."; rm -f spirv_reflect.zip; exit 1; fi

# The extraction creates a directory like SPIRV-Reflect-main
if [ -d "SPIRV-Reflect-${SPIRV_REFLECT_VERSION_TAG}" ]; then
    # Move required files to SPIRV-Reflect directory
    mkdir -p SPIRV-Reflect
    # Copy the key files
    cp "SPIRV-Reflect-${SPIRV_REFLECT_VERSION_TAG}/spirv_reflect.h" "SPIRV-Reflect/"
    cp "SPIRV-Reflect-${SPIRV_REFLECT_VERSION_TAG}/spirv_reflect.c" "SPIRV-Reflect/"
    cp -R "SPIRV-Reflect-${SPIRV_REFLECT_VERSION_TAG}/include" "SPIRV-Reflect/"
    # Copy license and readme for compliance
    cp "SPIRV-Reflect-${SPIRV_REFLECT_VERSION_TAG}/LICENSE" "SPIRV-Reflect/"
    cp "SPIRV-Reflect-${SPIRV_REFLECT_VERSION_TAG}/README.md" "SPIRV-Reflect/"
    # Clean up extracted directory
    rm -rf "SPIRV-Reflect-${SPIRV_REFLECT_VERSION_TAG}"
else
    echo "ERROR: Expected SPIRV-Reflect-${SPIRV_REFLECT_VERSION_TAG} directory not found after extraction."
    exit 1
fi

rm -f spirv_reflect.zip
echo "SPIRV-Reflect setup complete."

# --- Final Directory Verification ---
echo
echo "--- Verifying final directory structure in $PWD ---"
echo "Your vendor_libraries.cmake file should be configured for these directory names."
EXPECTED_DIRS=("GLFW" "glm" "imgui" "entt" "spdlog" "stb_image" "VulkanMemoryAllocator" "SPIRV-Reflect")
ALL_FOUND=true
for DIR_NAME in "${EXPECTED_DIRS[@]}"; do
    if [ -d "$DIR_NAME" ]; then
        printf "  [FOUND]   %s\n" "$DIR_NAME"
    else
        printf "  [MISSING] %s --- Please check setup for this library!\n" "$DIR_NAME"
        ALL_FOUND=false
    fi
done

if [ "$ALL_FOUND" = true ]; then
    echo "--- Verification successful. All expected library directories found. ---"
else
    echo "--- Verification failed. Some library directories are missing. ---"
    # Do not exit with error, as some might be optional or intentionally skipped later
fi


cd ../.. # Go back to project root from Engine/vendor
echo
echo "All specified vendor libraries have been processed from $PWD."
echo "Script finished."
exit 0 