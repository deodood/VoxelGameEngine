cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project(VoxelGameEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
if(EMSCRIPTEN)
    # bgfx.cmake sees both the Windows host and Emscripten target in this setup.
    set(WIN32 FALSE)
    set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "Use a browser-compatible render loop" FORCE)
endif()

option(VOXEL_REBUILD_SHADERS "Compile renderer and game shader sources with bgfx shaderc" OFF)
set(VOXEL_ADDITIONAL_SHADER_DIR "" CACHE PATH "Optional game-owned directory with vs_*.sc and fs_*.sc files")
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "Disable upstream bgfx examples" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "Disable upstream bgfx tests" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "Disable unused bgfx binary converter" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "Disable unused bgfx geometry tools" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "Disable unused bgfx texture tools" FORCE)
if(VOXEL_REBUILD_SHADERS AND NOT EMSCRIPTEN)
    set(BGFX_BUILD_TOOLS ON CACHE BOOL "Build bgfx shaderc" FORCE)
    set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "Build bgfx shaderc" FORCE)
else()
    set(BGFX_BUILD_TOOLS OFF CACHE BOOL "Use checked-in shader binaries" FORCE)
    set(BGFX_BUILD_TOOLS_SHADER OFF CACHE BOOL "Use checked-in shader binaries" FORCE)
endif()
set(BGFX_INSTALL OFF CACHE BOOL "Disable bgfx installation rules" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "Disable upstream aggregate targets" FORCE)

FetchContent_Declare(
    bgfx
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake
    GIT_TAG v1.129.8925-494
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(bgfx)
if(TARGET bimg_encode)
    set_target_properties(bimg_encode PROPERTIES EXCLUDE_FROM_ALL ON)
endif()

add_library(VoxelRenderer STATIC
    src/Renderer.cpp
    include/voxel/Renderer.hpp
)
add_library(VoxelRenderer::VoxelRenderer ALIAS VoxelRenderer)
target_include_directories(VoxelRenderer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(VoxelRenderer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(VoxelRenderer PUBLIC bgfx bimg bimg_decode bx)
target_compile_features(VoxelRenderer PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(VoxelRenderer PRIVATE /W4 /permissive-)
else()
    target_compile_options(VoxelRenderer PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(VOXEL_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
set(VOXEL_SHADER_BIN_DIR "${VOXEL_SHADER_SOURCE_DIR}/bin")
set(VOXEL_VARYING_DEF "${VOXEL_SHADER_SOURCE_DIR}/varying.def.sc")

file(GLOB VOXEL_SHADER_SOURCES CONFIGURE_DEPENDS
    "${VOXEL_SHADER_SOURCE_DIR}/vs_*.sc"
    "${VOXEL_SHADER_SOURCE_DIR}/fs_*.sc"
)
if(VOXEL_ADDITIONAL_SHADER_DIR)
    if(NOT IS_DIRECTORY "${VOXEL_ADDITIONAL_SHADER_DIR}")
        message(FATAL_ERROR "VOXEL_ADDITIONAL_SHADER_DIR is not a directory: ${VOXEL_ADDITIONAL_SHADER_DIR}")
    endif()
    file(GLOB VOXEL_ADDITIONAL_SHADER_SOURCES CONFIGURE_DEPENDS
        "${VOXEL_ADDITIONAL_SHADER_DIR}/vs_*.sc"
        "${VOXEL_ADDITIONAL_SHADER_DIR}/fs_*.sc"
    )
    list(APPEND VOXEL_SHADER_SOURCES ${VOXEL_ADDITIONAL_SHADER_SOURCES})
endif()
set(VOXEL_SHADER_NAMES)
set(VOXEL_EMBEDDED_SHADER_BINARIES)
foreach(shaderSource IN LISTS VOXEL_SHADER_SOURCES)
    get_filename_component(shaderName "${shaderSource}" NAME_WE)
    if(shaderName IN_LIST VOXEL_SHADER_NAMES)
        message(FATAL_ERROR "Duplicate shader name '${shaderName}'. Shader names must be unique across source directories.")
    endif()
    list(APPEND VOXEL_SHADER_NAMES "${shaderName}")
    list(APPEND VOXEL_EMBEDDED_SHADER_BINARIES
        "${VOXEL_SHADER_BIN_DIR}/dx11/${shaderName}.bin"
        "${VOXEL_SHADER_BIN_DIR}/essl/${shaderName}.bin"
    )
endforeach()
set(VOXEL_SHADER_OUTPUTS)

if(VOXEL_REBUILD_SHADERS AND NOT EMSCRIPTEN)
    if(NOT TARGET shaderc)
        message(FATAL_ERROR "bgfx shaderc is required. Reconfigure so bgfx.cmake builds the shader tools.")
    endif()
    foreach(shaderSource IN LISTS VOXEL_SHADER_SOURCES)
        get_filename_component(shaderName "${shaderSource}" NAME_WE)
        if(shaderName MATCHES "^vs_")
            set(shaderType vertex)
        else()
            set(shaderType fragment)
        endif()
        set(dx11Output "${VOXEL_SHADER_BIN_DIR}/dx11/${shaderName}.bin")
        set(esslOutput "${VOXEL_SHADER_BIN_DIR}/essl/${shaderName}.bin")
        list(APPEND VOXEL_SHADER_OUTPUTS "${dx11Output}" "${esslOutput}")
        add_custom_command(
            OUTPUT "${dx11Output}" "${esslOutput}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VOXEL_SHADER_BIN_DIR}/dx11"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VOXEL_SHADER_BIN_DIR}/essl"
            COMMAND "$<TARGET_FILE:shaderc>" -f "${shaderSource}" -o "${dx11Output}" --type "${shaderType}" --platform windows --profile s_5_0 --varyingdef "${VOXEL_VARYING_DEF}" -i "${bgfx_SOURCE_DIR}/bgfx/src"
            COMMAND "$<TARGET_FILE:shaderc>" -f "${shaderSource}" -o "${esslOutput}" --type "${shaderType}" --platform android --profile 100_es --varyingdef "${VOXEL_VARYING_DEF}" -i "${bgfx_SOURCE_DIR}/bgfx/src"
            DEPENDS shaderc "${shaderSource}" "${VOXEL_VARYING_DEF}"
            VERBATIM
            COMMENT "Compiling ${shaderName} for D3D11 and WebGL 2"
        )
    endforeach()
    add_custom_target(voxel_shaders DEPENDS ${VOXEL_SHADER_OUTPUTS})
else()
    foreach(shaderSource IN LISTS VOXEL_SHADER_SOURCES)
        get_filename_component(shaderName "${shaderSource}" NAME_WE)
        foreach(backend dx11 essl)
            set(shaderBinary "${VOXEL_SHADER_BIN_DIR}/${backend}/${shaderName}.bin")
            if(NOT EXISTS "${shaderBinary}")
                message(FATAL_ERROR "Missing precompiled shader ${shaderBinary}. Configure with -DVOXEL_REBUILD_SHADERS=ON and build voxel_shaders.")
            endif()
        endforeach()
    endforeach()
    add_custom_target(voxel_shaders SOURCES ${VOXEL_SHADER_SOURCES} "${VOXEL_VARYING_DEF}")
endif()

# Embed the built-in lit shader variants in VoxelRenderer for every platform.
set(VOXEL_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(VOXEL_EMBEDDED_SHADERS_HEADER "${VOXEL_GENERATED_INCLUDE_DIR}/voxel/EmbeddedShaders.hpp")
add_custom_command(
    OUTPUT "${VOXEL_EMBEDDED_SHADERS_HEADER}"
    COMMAND ${CMAKE_COMMAND}
        "-DOUTPUT_FILE=${VOXEL_EMBEDDED_SHADERS_HEADER}"
        "-DSHADER_NAMES=${VOXEL_SHADER_NAMES}"
        "-DDX11_SHADER_DIR=${VOXEL_SHADER_BIN_DIR}/dx11"
        "-DESSL_SHADER_DIR=${VOXEL_SHADER_BIN_DIR}/essl"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
        ${VOXEL_EMBEDDED_SHADER_BINARIES}
    VERBATIM
    COMMENT "Embedding the built-in renderer shaders"
)
add_custom_target(voxel_embedded_shaders DEPENDS "${VOXEL_EMBEDDED_SHADERS_HEADER}")
add_dependencies(voxel_embedded_shaders voxel_shaders)
add_dependencies(VoxelRenderer voxel_embedded_shaders)

option(VOXEL_BUILD_EXAMPLE "Build the standalone renderer example" ${PROJECT_IS_TOP_LEVEL})
if(VOXEL_BUILD_EXAMPLE)
    if(EMSCRIPTEN)
        set(VOXEL_PLATFORM_SOURCE examples/platform/PlatformEmscripten.cpp)
    else()
        set(VOXEL_PLATFORM_SOURCE examples/platform/PlatformWin32.cpp)
    endif()

    add_executable(VoxelRendererExample
        examples/main.cpp
        examples/Basic3D.cpp
        examples/Basic3D.hpp
        examples/platform/Platform.hpp
        ${VOXEL_PLATFORM_SOURCE}
    )
    target_include_directories(VoxelRendererExample PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/examples"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/platform"
    )
    target_link_libraries(VoxelRendererExample PRIVATE VoxelRenderer::VoxelRenderer)
    target_compile_features(VoxelRendererExample PRIVATE cxx_std_20)
    # Keep the example's previous filename so an incremental build replaces
    # old example binaries instead of leaving a stale executable beside it.
    set_target_properties(VoxelRendererExample PROPERTIES OUTPUT_NAME "VoxelGameEngine")
    add_dependencies(VoxelRendererExample voxel_shaders)

    if(WIN32)
        target_link_libraries(VoxelRendererExample PRIVATE user32 gdi32)
    endif()

    if(EMSCRIPTEN)
        set_target_properties(VoxelRendererExample PROPERTIES SUFFIX ".html")
        target_link_options(VoxelRendererExample PRIVATE
            "-sUSE_WEBGL2=1"
            "-sMIN_WEBGL_VERSION=2"
            "-sMAX_WEBGL_VERSION=2"
            "-sALLOW_MEMORY_GROWTH=1"
            "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/assets/web/shell.html"
        )
    else()
        # The example has no engine-owned runtime files, so its working
        # directory does not affect startup.
    endif()
endif()
