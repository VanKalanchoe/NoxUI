cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendors/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vendors/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

project(NoxUI VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set(CMAKE_VS_GLOBALS "UseMultiToolTask=true" "EnforceProcessCountAcrossBuilds=true")
endif()

option(NOXUI_BUILD_SDL3_BACKEND   "Provide header-only SDL3 platform backend target (NoxUI::SDL3)" ON)
option(NOXUI_BUILD_NOXRHI_BACKEND "Provide header-only NoxRHI renderer backend target (NoxUI::NoxRHI)" ON)
option(NOXUI_BUILD_EXAMPLES       "Build standalone NoxUI examples (UIDemo)" ON)

set(NOXRHI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendors/NoxRHI" CACHE PATH "Path to NoxRHI repository")

# -----------------------------------------------------------------------------
# Third-Party Dependencies for Core NoxUI
# -----------------------------------------------------------------------------
find_package(glm CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Freetype REQUIRED)

set(MSDF_ATLAS_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_VCPKG OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_SKIA OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_NO_ARTERY_FONT ON CACHE BOOL "" FORCE)
set(MSDF_ATLAS_DYNAMIC_RUNTIME ON CACHE BOOL "" FORCE)
set(MSDFGEN_DYNAMIC_RUNTIME ON CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_SVG ON CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_PNG ON CACHE BOOL "" FORCE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendors/msdf-atlas-gen/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/vendors/msdf-atlas-gen" EXCLUDE_FROM_ALL)
    if(TARGET msdfgen-core)
        target_compile_definitions(msdfgen-core PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    if(TARGET msdfgen-ext)
        target_compile_definitions(msdfgen-ext PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    if(TARGET msdf-atlas-gen)
        target_compile_definitions(msdf-atlas-gen PUBLIC _CRT_SECURE_NO_WARNINGS)
        target_include_directories(msdf-atlas-gen SYSTEM INTERFACE
            "${CMAKE_CURRENT_SOURCE_DIR}/vendors/msdf-atlas-gen"
            "${CMAKE_CURRENT_SOURCE_DIR}/vendors/msdf-atlas-gen/msdfgen"
        )
    endif()
else()
    message(FATAL_ERROR "msdf-atlas-gen not found in vendors/msdf-atlas-gen. Run scripts/install_dependencies.bat first.")
endif()

# -----------------------------------------------------------------------------
# Layer 1: Core NoxUI Library (Zero SDL3 / Zero GPU API dependencies)
# -----------------------------------------------------------------------------
file(GLOB_RECURSE NOXUI_CORE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/NoxUI/*.h"
)
list(FILTER NOXUI_CORE_HEADERS EXCLUDE REGEX ".*/Backends/.*")

file(GLOB_RECURSE NOXUI_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)
list(FILTER NOXUI_CORE_SOURCES EXCLUDE REGEX ".*/Backends/.*")

add_library(NoxUI STATIC ${NOXUI_CORE_HEADERS} ${NOXUI_CORE_SOURCES})
add_library(NoxUI::Core ALIAS NoxUI)

target_include_directories(NoxUI
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(NoxUI PUBLIC cxx_std_20)
target_compile_definitions(NoxUI
    PUBLIC
        GLM_FORCE_EXPLICIT_CTOR
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_ENABLE_EXPERIMENTAL
    PRIVATE
        $<$<BOOL:${WIN32}>:NOMINMAX>
        $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>
        _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(NoxUI
    PUBLIC
        glm::glm
    PRIVATE
        yaml-cpp::yaml-cpp
        msdf-atlas-gen::msdf-atlas-gen
)

# -----------------------------------------------------------------------------
# Layer 2: Optional Platform & Renderer Backends (ImGui-style header-only)
# -----------------------------------------------------------------------------
if(NOXUI_BUILD_SDL3_BACKEND)
    find_package(SDL3 CONFIG REQUIRED)
    add_library(NoxUI_SDL3 INTERFACE)
    add_library(NoxUI::SDL3 ALIAS NoxUI_SDL3)
    target_include_directories(NoxUI_SDL3 INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(NoxUI_SDL3 INTERFACE NoxUI::Core SDL3::SDL3)
    target_compile_features(NoxUI_SDL3 INTERFACE cxx_std_20)
endif()

if(NOXUI_BUILD_NOXRHI_BACKEND)
    if(NOT TARGET NoxRHI::HPP)
        if(EXISTS "${NOXRHI_DIR}/CMakeLists.txt")
            set(NRI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(NRI_ENABLE_STREAMLINE OFF CACHE BOOL "" FORCE)
            set(NRI_ENABLE_NRD OFF CACHE BOOL "" FORCE)
            add_subdirectory("${NOXRHI_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/NoxRHI" EXCLUDE_FROM_ALL)
        else()
            message(FATAL_ERROR "NoxRHI not found at NOXRHI_DIR='${NOXRHI_DIR}'. Run scripts/install_dependencies.bat or pass -DNOXRHI_DIR=<path> to CMake.")
        endif()
    endif()

    add_library(NoxUI_NoxRHI INTERFACE)
    add_library(NoxUI::NoxRHI ALIAS NoxUI_NoxRHI)
    target_include_directories(NoxUI_NoxRHI INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(NoxUI_NoxRHI INTERFACE NoxUI::Core NoxRHI::HPP)
    target_compile_features(NoxUI_NoxRHI INTERFACE cxx_std_20)
endif()

# -----------------------------------------------------------------------------
# Layer 3: Standalone Examples (examples/UIDemo)
# -----------------------------------------------------------------------------
if(NOXUI_BUILD_EXAMPLES)
    if(NOT NOXUI_BUILD_SDL3_BACKEND OR NOT NOXUI_BUILD_NOXRHI_BACKEND)
        message(FATAL_ERROR "NOXUI_BUILD_EXAMPLES requires NOXUI_BUILD_SDL3_BACKEND=ON and NOXUI_BUILD_NOXRHI_BACKEND=ON")
    endif()

    find_package(Stb QUIET)

    add_executable(UIDemo examples/UIDemo/main.cpp)
    if(Stb_FOUND OR DEFINED Stb_INCLUDE_DIR)
        target_include_directories(UIDemo SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
        target_compile_definitions(UIDemo PRIVATE NOXUI_HAS_STB=1 _CRT_SECURE_NO_WARNINGS)
    endif()
    target_link_libraries(UIDemo PRIVATE
        NoxUI::Core
        NoxUI::SDL3
        NoxUI::NoxRHI
        NoxRHI::RenderGraph
    )

    if(WIN32)
        if(TARGET NoxRHI AND NOT NRI_STATIC_LIBRARY)
            add_custom_command(TARGET UIDemo POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:NoxRHI>
                    $<TARGET_FILE_DIR:UIDemo>
            )
        endif()
        add_custom_command(TARGET UIDemo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE_DIR:UIDemo>
        )
        if(EXISTS "${NOXRHI_DIR}/vendors/slang/bin/slang.dll")
            add_custom_command(TARGET UIDemo POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${NOXRHI_DIR}/vendors/slang/bin/slang.dll"
                    $<TARGET_FILE_DIR:UIDemo>
            )
            file(GLOB SLANG_STD_MODULES LIST_DIRECTORIES true "${NOXRHI_DIR}/vendors/slang/bin/slang-standard-module-*")
            foreach(SLANG_MOD_DIR ${SLANG_STD_MODULES})
                get_filename_component(SLANG_MOD_NAME "${SLANG_MOD_DIR}" NAME)
                add_custom_command(TARGET UIDemo POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                        "${SLANG_MOD_DIR}"
                        "$<TARGET_FILE_DIR:UIDemo>/${SLANG_MOD_NAME}"
                )
            endforeach()
        endif()
    endif()

    add_custom_command(TARGET UIDemo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:UIDemo>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:UIDemo>/shaders"
    )
endif()
