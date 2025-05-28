@echo off
setlocal

if not exist "Engine" (
    echo ERROR: Script not in project root or Engine dir missing.
    echo Please run this script from the root of your RaptureVK project.
    pause
    exit /b 1
)
echo Script is running from the project root.

REM --- Prerequisites Check --- 
echo Ensuring prerequisites (git, curl, tar) are available...
CALL :CheckCommandExists git
CALL :CheckCommandExists curl
CALL :CheckCommandExists tar
echo All prerequisites found.

REM --- Configuration (Define versions and URLs first for summary) ---
set VENDOR_DIR_NAME=Engine\vendor
set GLFW_VERSION=3.4
set GLM_VERSION=1.0.1
set ENTT_VERSION_TAG=v3.13.0
set SPDLOG_VERSION_TAG=v1.14.1
set STB_IMAGE_VERSION=master
set VMA_VERSION_TAG=v3.3.0
set SPIRV_REFLECT_VERSION_TAG=main
set YYJSON_VERSION_TAG=0.11.1

set GLFW_URL=https://github.com/glfw/glfw/releases/download/%GLFW_VERSION%/glfw-%GLFW_VERSION%.zip
set GLM_URL=https://github.com/g-truc/glm/archive/refs/tags/%GLM_VERSION%.zip
set IMGUI_REPO=https://github.com/ocornut/imgui.git
set IMGUI_BRANCH=docking
set ENTT_URL=https://github.com/skypjack/entt/archive/refs/tags/%ENTT_VERSION_TAG%.zip
set SPDLOG_URL=https://github.com/gabime/spdlog/archive/refs/tags/%SPDLOG_VERSION_TAG%.zip
set STB_IMAGE_H_URL=https://raw.githubusercontent.com/nothings/stb/%STB_IMAGE_VERSION%/stb_image.h
set VMA_URL=https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/%VMA_VERSION_TAG%.zip
set SPIRV_REFLECT_URL=https://github.com/KhronosGroup/SPIRV-Reflect/archive/refs/heads/%SPIRV_REFLECT_VERSION_TAG%.zip
set YYJSON_URL=https://github.com/ibireme/yyjson/archive/refs/tags/%YYJSON_VERSION_TAG%.zip

REM --- Display Summary and Ask for Confirmation ---
echo.
echo The following libraries will be downloaded/set up:
echo ================================================================================
echo Library       Version/Branch      Source
echo ------------- ------------------- --------------------------------------------------
CALL :PrintLibInfo "GLFW" "%GLFW_VERSION%" "%GLFW_URL%"
CALL :PrintLibInfo "GLM" "%GLM_VERSION%" "%GLM_URL%"
CALL :PrintLibInfo "ImGui" "%IMGUI_BRANCH% (branch)" "%IMGUI_REPO%"
CALL :PrintLibInfo "EnTT" "%ENTT_VERSION_TAG%" "%ENTT_URL%"
CALL :PrintLibInfo "spdlog" "%SPDLOG_VERSION_TAG%" "%SPDLOG_URL%"
CALL :PrintLibInfo "stb_image" "%STB_IMAGE_VERSION% (tag/commit)" "%STB_IMAGE_H_URL%"
CALL :PrintLibInfo "VMA" "%VMA_VERSION_TAG%" "%VMA_URL%"
CALL :PrintLibInfo "SPIRV-Reflect" "%SPIRV_REFLECT_VERSION_TAG%" "%SPIRV_REFLECT_URL%"
CALL :PrintLibInfo "yyjson" "%YYJSON_VERSION_TAG%" "%YYJSON_URL%"
echo ================================================================================

REM Get the initial full path of the target vendor directory for display
for %%A in ("%~dp0%VENDOR_DIR_NAME%") do set "EXPECTED_ABS_VENDOR_DIR=%%~fA"
echo All libraries will be placed in: %EXPECTED_ABS_VENDOR_DIR%
echo.

choice /c YN /m "Proceed with setup? (Y/N)"
if errorlevel 2 (  REM N is chosen (errorlevel 2 for N, 1 for Y)
    echo Setup cancelled by user.
    goto :eof
)
if errorlevel 1 (  REM Y is chosen
    echo Starting library setup...
)


REM --- Create Vendor Directory ---
echo Creating vendor directory: %EXPECTED_ABS_VENDOR_DIR%
mkdir "%EXPECTED_ABS_VENDOR_DIR%" 2>nul
if not exist "%EXPECTED_ABS_VENDOR_DIR%" (
    echo ERROR: Failed to create directory %EXPECTED_ABS_VENDOR_DIR%. Check permissions.
    pause
    exit /b 1
)

REM --- Change to Vendor Directory ---
echo Changing to vendor directory: %EXPECTED_ABS_VENDOR_DIR%
cd /D "%EXPECTED_ABS_VENDOR_DIR%"
if errorlevel 1 (
    echo ERROR: Failed to change directory to "%EXPECTED_ABS_VENDOR_DIR%" using 'cd /D'.
    echo Current directory is: %CD%
    pause
    exit /b 1
)

REM Verify current directory more simply
if /I not "%CD%\"=="%EXPECTED_ABS_VENDOR_DIR%\" (
    echo ERROR: Directory change was not successful or path mismatch.
    echo Current directory : %CD%
    echo Expected directory: %EXPECTED_ABS_VENDOR_DIR%
    pause
    exit /b 1
)
echo Successfully changed into %CD%


REM --- Download and Setup Libraries (Actual commands) ---

REM --- GLFW ---
echo.
echo Setting up GLFW %GLFW_VERSION%...
if exist GLFW rmdir /s /q GLFW 2>nul REM Clean up old directory before extraction
if exist glfw-%GLFW_VERSION% rmdir /s /q glfw-%GLFW_VERSION% 2>nul REM Clean up old extracted dir
curl -L %GLFW_URL% -o glfw.zip
if errorlevel 1 ( echo ERROR: Failed to download GLFW. && pause && exit /b 1 )
echo Extracting GLFW...
tar -xf glfw.zip
if errorlevel 1 ( echo ERROR: Failed to extract GLFW. && pause && exit /b 1 )
ren glfw-%GLFW_VERSION% GLFW
del glfw.zip
echo GLFW setup complete.

REM --- GLM ---
echo.
echo Setting up GLM %GLM_VERSION%...
if exist glm rmdir /s /q glm 2>nul REM Clean up old directory
if exist glm-%GLM_VERSION% rmdir /s /q glm-%GLM_VERSION% 2>nul REM Clean up old extracted dir
curl -L %GLM_URL% -o glm.zip
if errorlevel 1 ( echo ERROR: Failed to download GLM. && pause && exit /b 1 )
echo Extracting GLM...
tar -xf glm.zip
if errorlevel 1 ( echo ERROR: Failed to extract GLM. && pause && exit /b 1 )
move glm-%GLM_VERSION%\glm .\glm
rmdir /s /q glm-%GLM_VERSION%
del glm.zip
echo GLM setup complete.

REM --- ImGui (docking branch) ---
echo.
echo Setting up ImGui (%IMGUI_BRANCH% branch)...
echo Removing existing ImGui directory if present...
if exist imgui rmdir /s /q imgui
echo Cloning ImGui from %IMGUI_REPO%...
git clone --branch %IMGUI_BRANCH% %IMGUI_REPO% imgui --depth 1
if errorlevel 1 ( echo ERROR: Failed to clone ImGui. && pause && exit /b 1 )
echo ImGui setup complete.

REM --- EnTT ---
echo.
set ENTT_VERSION_NO_V=%ENTT_VERSION_TAG:v=%
echo Setting up EnTT %ENTT_VERSION_TAG% (extracted dir expected: entt-%ENTT_VERSION_NO_V%)...
if exist entt rmdir /s /q entt 2>nul REM Clean up old directory
if exist entt-%ENTT_VERSION_NO_V% rmdir /s /q entt-%ENTT_VERSION_NO_V% 2>nul REM Clean up old extracted dir (without v)
curl -L %ENTT_URL% -o entt.zip
if errorlevel 1 ( echo ERROR: Failed to download EnTT. && pause && exit /b 1 )
echo Extracting EnTT...
tar -xf entt.zip
if errorlevel 1 ( echo ERROR: Failed to extract EnTT. && pause && exit /b 1 )

REM Check if the expected extracted directory exists
if not exist "entt-%ENTT_VERSION_NO_V%" (
    echo ERROR: Expected extracted directory "entt-%ENTT_VERSION_NO_V%" not found.
    echo Please check the contents of entt.zip or the ENTT_VERSION_TAG.
    pause
    exit /b 1
)

REM Create target structure: entt/include/entt/
mkdir entt\include\entt 2>nul
if not exist "entt\include\entt" (
    echo ERROR: Failed to create directory entt\include\entt. Check permissions.
    pause
    exit /b 1
)

REM Check if single include header exists in the extracted archive
if not exist "entt-%ENTT_VERSION_NO_V%\single_include\entt\entt.hpp" (
    echo ERROR: EnTT single include header not found at "entt-%ENTT_VERSION_NO_V%\single_include\entt\entt.hpp".
    echo Listing contents of "entt-%ENTT_VERSION_NO_V%\single_include":
    dir "entt-%ENTT_VERSION_NO_V%\single_include" /s /b
    pause
    exit /b 1
)

move "entt-%ENTT_VERSION_NO_V%\single_include\entt\entt.hpp" "entt\include\entt\entt.hpp"
if errorlevel 1 ( 
    echo ERROR: Failed to move EnTT single include header from "entt-%ENTT_VERSION_NO_V%\single_include\entt\entt.hpp" to "entt\include\entt\entt.hpp".
    pause
    exit /b 1
)

rmdir /s /q "entt-%ENTT_VERSION_NO_V%"
del entt.zip
echo EnTT setup complete.

REM --- spdlog ---
echo.
set SPDLOG_VERSION_NO_V=%SPDLOG_VERSION_TAG:v=%
echo Setting up spdlog %SPDLOG_VERSION_TAG% (extracted dir expected: spdlog-%SPDLOG_VERSION_NO_V%)...
if exist spdlog rmdir /s /q spdlog 2>nul REM Clean up old directory
if exist spdlog-%SPDLOG_VERSION_NO_V% rmdir /s /q spdlog-%SPDLOG_VERSION_NO_V% 2>nul REM Clean up old extracted dir (without v)
curl -L %SPDLOG_URL% -o spdlog.zip
if errorlevel 1 ( echo ERROR: Failed to download spdlog. && pause && exit /b 1 )
echo Extracting spdlog...
tar -xf spdlog.zip
if errorlevel 1 ( echo ERROR: Failed to extract spdlog. && pause && exit /b 1 )

if not exist "spdlog-%SPDLOG_VERSION_NO_V%" (
    echo ERROR: Expected extracted directory "spdlog-%SPDLOG_VERSION_NO_V%" not found.
    echo Please check the contents of spdlog.zip or the SPDLOG_VERSION_TAG.
    pause
    exit /b 1
)
ren "spdlog-%SPDLOG_VERSION_NO_V%" spdlog
if errorlevel 1 ( 
    echo ERROR: Failed to rename "spdlog-%SPDLOG_VERSION_NO_V%" to spdlog.
    pause
    exit /b 1
)
del spdlog.zip
echo spdlog setup complete.

REM --- stb_image ---
echo.
echo Setting up stb_image (%STB_IMAGE_VERSION% tag/commit)...
if exist stb_image rmdir /s /q stb_image 2>nul REM Clean up old directory
mkdir stb_image 2>nul
curl -L %STB_IMAGE_H_URL% -o stb_image\stb_image.h
if errorlevel 1 ( echo ERROR: Failed to download stb_image.h. && pause && exit /b 1 )
echo Creating stb_image.cpp...
(
    echo #define STB_IMAGE_IMPLEMENTATION
    echo #include "stb_image.h"
) > stb_image\stb_image.cpp
echo stb_image setup complete.

REM --- Vulkan Memory Allocator (VMA) ---
echo.
set VMA_VERSION_NO_V=%VMA_VERSION_TAG:v=%
echo Setting up Vulkan Memory Allocator %VMA_VERSION_TAG% (extracted dir expected: VulkanMemoryAllocator-%VMA_VERSION_NO_V%)...
if exist VulkanMemoryAllocator rmdir /s /q VulkanMemoryAllocator 2>nul
if exist VulkanMemoryAllocator-%VMA_VERSION_NO_V% rmdir /s /q VulkanMemoryAllocator-%VMA_VERSION_NO_V% 2>nul
curl -L %VMA_URL% -o vma.zip
if errorlevel 1 ( echo ERROR: Failed to download Vulkan Memory Allocator. && pause && exit /b 1 )
echo Extracting Vulkan Memory Allocator...
tar -xf vma.zip
if errorlevel 1 ( echo ERROR: Failed to extract Vulkan Memory Allocator. && pause && exit /b 1 )

if not exist "VulkanMemoryAllocator-%VMA_VERSION_NO_V%" (
    echo ERROR: Expected extracted directory "VulkanMemoryAllocator-%VMA_VERSION_NO_V%" not found.
    pause
    exit /b 1
)
ren "VulkanMemoryAllocator-%VMA_VERSION_NO_V%" VulkanMemoryAllocator
if errorlevel 1 ( 
    echo ERROR: Failed to rename "VulkanMemoryAllocator-%VMA_VERSION_NO_V%" to VulkanMemoryAllocator.
    pause
    exit /b 1
)
del vma.zip
echo Vulkan Memory Allocator setup complete.

REM --- SPIRV-Reflect ---
echo.
echo Setting up SPIRV-Reflect (%SPIRV_REFLECT_VERSION_TAG% branch)...
if exist SPIRV-Reflect rmdir /s /q SPIRV-Reflect 2>nul
if exist SPIRV-Reflect-%SPIRV_REFLECT_VERSION_TAG% rmdir /s /q SPIRV-Reflect-%SPIRV_REFLECT_VERSION_TAG% 2>nul
curl -L %SPIRV_REFLECT_URL% -o spirv_reflect.zip
if errorlevel 1 ( echo ERROR: Failed to download SPIRV-Reflect. && pause && exit /b 1 )
echo Extracting SPIRV-Reflect...
tar -xf spirv_reflect.zip
if errorlevel 1 ( echo ERROR: Failed to extract SPIRV-Reflect. && pause && exit /b 1 )

if not exist "SPIRV-Reflect-%SPIRV_REFLECT_VERSION_TAG%" (
    echo ERROR: Expected extracted directory "SPIRV-Reflect-%SPIRV_REFLECT_VERSION_TAG%" not found.
    pause
    exit /b 1
)

REM Create target directory
mkdir SPIRV-Reflect 2>nul
if not exist "SPIRV-Reflect" (
    echo ERROR: Failed to create directory SPIRV-Reflect. Check permissions.
    pause
    exit /b 1
)

REM Copy key files
copy "SPIRV-Reflect-%SPIRV_REFLECT_VERSION_TAG%\spirv_reflect.h" "SPIRV-Reflect\"
copy "SPIRV-Reflect-%SPIRV_REFLECT_VERSION_TAG%\spirv_reflect.c" "SPIRV-Reflect\"
xcopy /E /I /Y "SPIRV-Reflect-%SPIRV_REFLECT_VERSION_TAG%\include" "SPIRV-Reflect\include"
copy "SPIRV-Reflect-%SPIRV_REFLECT_VERSION_TAG%\LICENSE" "SPIRV-Reflect\"
copy "SPIRV-Reflect-%SPIRV_REFLECT_VERSION_TAG%\README.md" "SPIRV-Reflect\"

REM Clean up
rmdir /s /q "SPIRV-Reflect-%SPIRV_REFLECT_VERSION_TAG%"
del spirv_reflect.zip
echo SPIRV-Reflect setup complete.

REM --- yyjson ---
echo.
set YYJSON_VERSION_NO_V=%YYJSON_VERSION_TAG:v=%
echo Setting up yyjson %YYJSON_VERSION_TAG% (extracted dir expected: yyjson-%YYJSON_VERSION_NO_V%)...
if exist yyjson rmdir /s /q yyjson 2>nul
if exist yyjson-%YYJSON_VERSION_NO_V% rmdir /s /q yyjson-%YYJSON_VERSION_NO_V% 2>nul
curl -L %YYJSON_URL% -o yyjson.zip
if errorlevel 1 ( echo ERROR: Failed to download yyjson. && pause && exit /b 1 )
echo Extracting yyjson...
tar -xf yyjson.zip
if errorlevel 1 ( echo ERROR: Failed to extract yyjson. && pause && exit /b 1 )

set EXTRACTED_YYJSON_DIR=yyjson-%YYJSON_VERSION_NO_V%
set TARGET_YYJSON_DIR=yyjson

if not exist "%EXTRACTED_YYJSON_DIR%" (
    echo ERROR: Expected extracted directory "%EXTRACTED_YYJSON_DIR%" not found for yyjson.
    pause
    exit /b 1
)

mkdir "%TARGET_YYJSON_DIR%" 2>nul

if not exist "%EXTRACTED_YYJSON_DIR%\src\yyjson.h" (
    echo ERROR: yyjson.h not found in %EXTRACTED_YYJSON_DIR%\src.
    pause
    exit /b 1
)
if not exist "%EXTRACTED_YYJSON_DIR%\src\yyjson.c" (
    echo ERROR: yyjson.c not found in %EXTRACTED_YYJSON_DIR%\src.
    pause
    exit /b 1
)

move "%EXTRACTED_YYJSON_DIR%\src\yyjson.h" "%TARGET_YYJSON_DIR%\"
move "%EXTRACTED_YYJSON_DIR%\src\yyjson.c" "%TARGET_YYJSON_DIR%\"

if exist "%EXTRACTED_YYJSON_DIR%\LICENSE" (
    copy "%EXTRACTED_YYJSON_DIR%\LICENSE" "%TARGET_YYJSON_DIR%\"
)

rmdir /s /q "%EXTRACTED_YYJSON_DIR%"
del yyjson.zip
echo yyjson setup complete.

REM --- Final Directory Verification ---
echo.
echo --- Verifying final directory structure in %CD% --- 
echo Your vendor_libraries.cmake file should be configured for these directory names.
set EXPECTED_DIRS=GLFW glm imgui entt spdlog stb_image VulkanMemoryAllocator SPIRV-Reflect yyjson
for %%D in (%EXPECTED_DIRS%) do (
    if exist "%%D" (
        echo   [FOUND]   %%D
    ) else (
        echo   [MISSING] %%D --- Please check setup for this library!
    )
)
echo --- Verification complete ---

cd ..
echo.
echo All specified vendor libraries have been processed.
echo Script finished. Press any key to exit...
pause
endlocal
goto :eof

REM ====================================================================
REM Subroutines
REM ====================================================================

:CheckCommandExists
REM %1 is the command name to check (e.g., git, curl, tar)
(where /q %1 >nul 2>nul) || (
    echo ERROR: Command '%1' not found in PATH. Please install it and ensure it is accessible.
    pause
    exit /b 1
)
goto :eof

:PrintLibInfo
REM %1 = Library Name, %2 = Version/Branch, %3 = URL/Repo
set "_libName=%~1                 "
set "_libVersion=%~2                  "
set "_libSource=%~3"
REM Pad/truncate: libName to 13 chars, version to 19 chars
set "_libName=%_libName:~0,13%"
set "_libVersion=%_libVersion:~0,19%"
echo %_libName%%_libVersion%%_libSource%
goto :eof 