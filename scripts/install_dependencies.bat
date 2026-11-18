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
:: 1. msdf-atlas-gen (v1.4, includes msdfgen submodule)
:: ----------------------------------------------------------------------------
set "MSDF_ATLAS_VERSION=v1.4"
set "MSDF_ATLAS_DIR=%VENDORS_DIR%\msdf-atlas-gen"
if not exist "%MSDF_ATLAS_DIR%\msdf-atlas-gen\msdf-atlas-gen.h" (
    echo [1/2] Cloning msdf-atlas-gen ^(%MSDF_ATLAS_VERSION%^)...
    if exist "%MSDF_ATLAS_DIR%" rmdir /s /q "%MSDF_ATLAS_DIR%"
    git clone --recursive --depth 1 --branch "%MSDF_ATLAS_VERSION%" https://github.com/Chlumsky/msdf-atlas-gen.git "%MSDF_ATLAS_DIR%"
    if errorlevel 1 (
        echo ERROR: Failed to clone msdf-atlas-gen.
        goto :fail
    )
) else (
    echo [1/2] msdf-atlas-gen already present, skipping.
)

:: ----------------------------------------------------------------------------
:: 2. vcpkg + glm, yaml-cpp, freetype, sdl3, stb (x64-windows)
:: ----------------------------------------------------------------------------
set "VCPKG_ROOT=%VENDORS_DIR%\vcpkg"
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    if not exist "%VCPKG_ROOT%\.git" (
        echo [2/2] Cloning vcpkg into "%VCPKG_ROOT%"...
        if exist "%VCPKG_ROOT%" rmdir /s /q "%VCPKG_ROOT%"
        git clone https://github.com/Microsoft/vcpkg.git "%VCPKG_ROOT%"
        if errorlevel 1 (
            echo ERROR: Failed to clone vcpkg.
            goto :fail
        )
    ) else (
        echo [2/2] vcpkg repository already cloned, bootstrapping...
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
    echo [2/2] vcpkg executable already present.
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
