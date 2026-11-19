@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "ROOT_DIR=%%~fI"
set "VENDORS_DIR=%ROOT_DIR%\vendors"

if not exist "%VENDORS_DIR%" mkdir "%VENDORS_DIR%"

echo ============================================================================
echo Installing NoxUI dependencies into:
echo   %VENDORS_DIR%
echo ============================================================================

:: ----------------------------------------------------------------------------
:: 1. NoxRHI (cloned into vendors\NoxRHI + core RHI dependencies: VMA-Hpp, D3D12MA, Slang)
:: ----------------------------------------------------------------------------
if not defined NOXRHI_GIT_URL set "NOXRHI_GIT_URL=https://github.com/VanKalanchoe/NoxRHI.git"
set "NOXRHI_DIR=%VENDORS_DIR%\NoxRHI"
set "SIBLING_NOXRHI=%ROOT_DIR%\..\NoxRHI"

if not exist "%NOXRHI_DIR%\CMakeLists.txt" (
    echo [1/3] Cloning NoxRHI into "%NOXRHI_DIR%"...
    if exist "%NOXRHI_DIR%" rmdir /s /q "%NOXRHI_DIR%"
    git clone --depth 1 "%NOXRHI_GIT_URL%" "%NOXRHI_DIR%" >nul 2>nul
    if errorlevel 1 (
        if exist "%SIBLING_NOXRHI%\.git" (
            echo       Remote "%NOXRHI_GIT_URL%" not reachable yet; cloning from local "%SIBLING_NOXRHI%"...
            git clone "%SIBLING_NOXRHI%" "%NOXRHI_DIR%"
            if errorlevel 1 (
                echo ERROR: Failed to clone NoxRHI.
                goto :fail
            )
        ) else (
            echo ERROR: Failed to clone NoxRHI from "%NOXRHI_GIT_URL%".
            goto :fail
        )
    )
) else (
    echo [1/3] NoxRHI already present in "%NOXRHI_DIR%", skipping clone.
)

set "NOXRHI_VENDORS=%NOXRHI_DIR%\vendors"
if not exist "%NOXRHI_VENDORS%" mkdir "%NOXRHI_VENDORS%"

:: 1a. VulkanMemoryAllocator-Hpp (v3.4.0+3)
set "VMA_HPP_VERSION=v3.4.0+3"
set "VMA_HPP_DIR=%NOXRHI_VENDORS%\VulkanMemoryAllocator-Hpp"
if not exist "%VMA_HPP_DIR%\include\vk_mem_alloc.hpp" (
    echo       Cloning VulkanMemoryAllocator-Hpp ^(%VMA_HPP_VERSION%^)...
    if exist "%VMA_HPP_DIR%" rmdir /s /q "%VMA_HPP_DIR%"
    git clone --recursive --depth 1 --branch "%VMA_HPP_VERSION%" https://github.com/YaaZ/VulkanMemoryAllocator-Hpp.git "%VMA_HPP_DIR%"
    if errorlevel 1 (
        echo ERROR: Failed to clone VulkanMemoryAllocator-Hpp.
        goto :fail
    )
)

:: 1b. D3D12MemoryAllocator (v3.1.0)
set "D3D12MA_VERSION=v3.1.0"
set "D3D12MA_DIR=%NOXRHI_VENDORS%\D3D12MemoryAllocator"
if not exist "%D3D12MA_DIR%\src\D3D12MemAlloc.cpp" (
    echo       Cloning D3D12MemoryAllocator ^(%D3D12MA_VERSION%^)...
    if exist "%D3D12MA_DIR%" rmdir /s /q "%D3D12MA_DIR%"
    git clone --depth 1 --branch "%D3D12MA_VERSION%" https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git "%D3D12MA_DIR%"
    if errorlevel 1 (
        echo ERROR: Failed to clone D3D12MemoryAllocator.
        goto :fail
    )
)

:: 1c. Slang (v2026.14.1 Windows x86_64 prebuilt binaries + headers)
set "SLANG_VERSION=2026.14.1"
set "SLANG_URL=https://github.com/shader-slang/slang/releases/download/v%SLANG_VERSION%/slang-%SLANG_VERSION%-windows-x86_64.zip"
set "SLANG_DIR=%NOXRHI_VENDORS%\slang"
set "SLANG_ZIP=%TEMP%\noxui-slang-%SLANG_VERSION%.zip"
if not exist "%SLANG_DIR%\lib\slang.lib" (
    echo       Downloading Slang %SLANG_VERSION%...
    curl -fSL "%SLANG_URL%" -o "%SLANG_ZIP%"
    if errorlevel 1 (
        echo ERROR: Failed to download Slang from %SLANG_URL%.
        goto :fail
    )
    if exist "%SLANG_DIR%" rmdir /s /q "%SLANG_DIR%"
    mkdir "%SLANG_DIR%"
    echo       Extracting Slang to "%SLANG_DIR%"...
    tar -xf "%SLANG_ZIP%" -C "%SLANG_DIR%"
    if errorlevel 1 (
        if exist "%SLANG_ZIP%" del /q "%SLANG_ZIP%"
        if exist "%SLANG_DIR%" rmdir /s /q "%SLANG_DIR%"
        echo ERROR: Failed to extract Slang archive.
        goto :fail
    )
    if exist "%SLANG_ZIP%" del /q "%SLANG_ZIP%"
)

:: ----------------------------------------------------------------------------
:: 2. msdf-atlas-gen (v1.4, includes msdfgen submodule)
:: ----------------------------------------------------------------------------
set "MSDF_ATLAS_VERSION=v1.4"
set "MSDF_ATLAS_DIR=%VENDORS_DIR%\msdf-atlas-gen"
if not exist "%MSDF_ATLAS_DIR%\msdf-atlas-gen\msdf-atlas-gen.h" (
    echo [2/3] Cloning msdf-atlas-gen ^(%MSDF_ATLAS_VERSION%^)...
    if exist "%MSDF_ATLAS_DIR%" rmdir /s /q "%MSDF_ATLAS_DIR%"
    git clone --recursive --depth 1 --branch "%MSDF_ATLAS_VERSION%" https://github.com/Chlumsky/msdf-atlas-gen.git "%MSDF_ATLAS_DIR%"
    if errorlevel 1 (
        echo ERROR: Failed to clone msdf-atlas-gen.
        goto :fail
    )
) else (
    echo [2/3] msdf-atlas-gen already present, skipping.
)

:: ----------------------------------------------------------------------------
:: 3. vcpkg + glm, yaml-cpp, freetype, sdl3, stb (x64-windows)
:: ----------------------------------------------------------------------------
set "VCPKG_ROOT=%VENDORS_DIR%\vcpkg"
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    if not exist "%VCPKG_ROOT%\.git" (
        echo [3/3] Cloning vcpkg into "%VCPKG_ROOT%"...
        if exist "%VCPKG_ROOT%" rmdir /s /q "%VCPKG_ROOT%"
        git clone https://github.com/Microsoft/vcpkg.git "%VCPKG_ROOT%"
        if errorlevel 1 (
            echo ERROR: Failed to clone vcpkg.
            goto :fail
        )
    ) else (
        echo [3/3] vcpkg repository already cloned, bootstrapping...
    )
    pushd "%VCPKG_ROOT%"
    call bootstrap-vcpkg.bat -disableMetrics
    if errorlevel 1 (
        popd
        echo ERROR: Failed to bootstrap vcpkg.
        goto :fail
    )
    popd
) else (
    echo [3/3] vcpkg executable already present.
)

if not exist "%TEMP%\vcpkg-cache" mkdir "%TEMP%\vcpkg-cache"
set "VCPKG_BINARY_SOURCES=clear;files,%TEMP%\vcpkg-cache,readwrite"

echo Ensuring glm, yaml-cpp, freetype, sdl3, and stb ^(x64-windows^) are installed via vcpkg...
"%VCPKG_ROOT%\vcpkg.exe" install glm yaml-cpp freetype sdl3 stb --triplet=x64-windows
if errorlevel 1 (
    echo ERROR: Failed to install vcpkg packages.
    goto :fail
)

echo.
echo All NoxUI dependencies are ready in "%VENDORS_DIR%".
if /i not "%NRI_NO_PAUSE%"=="1" if /i not "%NOXUI_NO_PAUSE%"=="1" pause
exit /b 0

:fail
echo.
echo Dependency installation failed.
if /i not "%NRI_NO_PAUSE%"=="1" if /i not "%NOXUI_NO_PAUSE%"=="1" pause
exit /b 1
