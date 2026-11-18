@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "ROOT_DIR=%%~fI"
set "BUILD_DIR=%ROOT_DIR%\build"
set "VCPKG_TOOLCHAIN=%ROOT_DIR%\vendors\vcpkg\scripts\buildsystems\vcpkg.cmake"

:: Pass "--msvc" as first argument to use the default MSVC toolset instead of ClangCL,
:: and forward any additional flags directly to CMake (e.g. -DNOXUI_BUILD_EXAMPLES=OFF).
set "TOOLSET_ARGS=-T ClangCL"
if /i "%~1"=="--msvc" (
    set "TOOLSET_ARGS="
    shift
)

set "EXTRA_ARGS="
:collect_args
if "%~1"=="" goto :args_done
set "EXTRA_ARGS=%EXTRA_ARGS% %1"
shift
goto :collect_args
:args_done

set "TOOLCHAIN_ARG="
if exist "%VCPKG_TOOLCHAIN%" (
    set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_TOOLCHAIN%"
)

if defined TOOLSET_ARGS (
    echo Configuring NoxUI with Visual Studio 18 2026 + ClangCL...
) else (
    echo Configuring NoxUI with Visual Studio 18 2026 + MSVC...
)

cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 18 2026" %TOOLSET_ARGS% -A x64 %TOOLCHAIN_ARG% %EXTRA_ARGS%
if errorlevel 1 (
    echo.
    echo CMake configuration failed. Make sure you ran scripts\install_dependencies.bat first.
    if /i not "%NRI_NO_PAUSE%"=="1" if /i not "%NOXUI_NO_PAUSE%"=="1" pause
    exit /b 1
)

echo.
echo Solution generated: "%BUILD_DIR%\NoxUI.slnx"
echo Open it in JetBrains Rider or Visual Studio 2026 to build and run.
if /i not "%NRI_NO_PAUSE%"=="1" if /i not "%NOXUI_NO_PAUSE%"=="1" pause
exit /b 0
