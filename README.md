# VoxelRenderer

VoxelRenderer is a small C++20 3D rendering library built on bgfx. It currently builds for Windows with MSVC and for browsers with Emscripten/WebGL 2.

Your game owns its window or canvas, input, game loop, and gameplay. VoxelRenderer takes the native window/canvas handle, manages rendering resources, and submits meshes to bgfx. The Windows and browser window/input code is part of the example app, not the renderer library.

## What is included

- A CMake library target named `VoxelRenderer::VoxelRenderer`.
- A basic 3D mesh format, indexed mesh upload, and a built-in cube mesh.
- 2D textures loaded from image files or created from RGBA8 pixels, with mipmaps generated on upload.
- A named texture atlas with pixel-coordinate regions and UVs.
- Materials with the built-in directional diffuse/specular lighting shader, texture samplers, and custom bgfx shaders.
- Camera, transform, resize, clear color, and draw submission helpers.
- A standalone example app for Windows and the browser.

The built-in shader binaries are embedded in the library when it is built. The sample creates its atlas in memory. Running either example does not require an engine `assets` directory beside the executable or a browser `.data` asset package.

## Requirements

### Windows

- CMake 3.25 or newer to use the included CMake presets. (The `CMakeLists.txt` file itself requires CMake 3.21 or newer.)
- Visual Studio with the Desktop development with C++ workload.
- Ninja available in the Visual Studio Developer PowerShell environment.
- Git and an internet connection when configuring a new build folder, so CMake can fetch bgfx.cmake and its dependencies. Configuring again in the same folder reuses the downloaded sources.

Run the Windows commands below from **Developer PowerShell for Visual Studio**, at the project root.

### Web

- The Emscripten SDK (`emsdk`) installed and activated.
- CMake 3.25 or newer to use the included presets, Python 3, and Git. The project itself requires CMake 3.21 or newer.
- An internet connection on the first configure if bgfx dependencies are not already cached.

On Windows, open Command Prompt, change to your Emscripten SDK folder, and run `emsdk_env.bat`. Stay in that same Command Prompt, change to the project root, and run the web commands below. Check that `emcc -v` works before configuring.

## Run the Windows example

Build and launch the Debug example from Developer PowerShell:

```powershell
cmake --preset msvc-ninja
cmake --build --preset msvc-ninja-debug --target VoxelRendererExample --parallel
.\out\build\msvc-ninja\VoxelGameEngine.exe
```

For a separate Release build, use another build folder:

```powershell
cmake -S . -B out/build/msvc-ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/msvc-ninja-release --target VoxelRendererExample --parallel
.\out\build\msvc-ninja-release\VoxelGameEngine.exe
```

The CMake target is `VoxelRendererExample`; the output is named `VoxelGameEngine.exe`. The example opens a window and keeps running until you close it or press **Escape**. Use **W/A/S/D** to move, **Space** and **Shift** to move vertically, and click the window to capture the mouse for looking around.

The library itself is the `VoxelRenderer` target and produces a static `.lib`. The `.exe` is the sample game that links that library; it is not the renderer library.

## Run the browser example

In a terminal where `emsdk_env.bat` has been run, build the Debug web example:

```cmd
cmake --preset web-debug
cmake --build --preset web-debug --target VoxelRendererExample --parallel
```

Serve the generated files over HTTP from the project root:

```cmd
python -m http.server 8080 -d out/build/web-debug-renderer
```

Open [http://localhost:8080/VoxelGameEngine.html](http://localhost:8080/VoxelGameEngine.html). Keep the generated `.html`, `.js`, and `.wasm` files together in the output folder. A browser will not load WebAssembly from a `file://` path. This example uses no external asset package.

To build and serve the optimized version instead, use `web-release` and `out/build/web-release-renderer`:

```cmd
cmake --preset web-release
cmake --build --preset web-release --target VoxelRendererExample --parallel
python -m http.server 8080 -d out/build/web-release-renderer
```

The browser controls match the Windows example: **W/A/S/D**, **Space**, and **Shift** move; click the canvas to capture the mouse; **Escape** exits the example.

## Build only the library

Configure once for the platform, then build the `VoxelRenderer` target instead of the example target.

### Windows Debug

```powershell
cmake --preset msvc-ninja
cmake --build --preset msvc-ninja-debug --target VoxelRenderer --parallel
```

The static library is `VoxelRenderer.lib` under `out/build/msvc-ninja`.

### Windows Release

```powershell
cmake -S . -B out/build/msvc-ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/msvc-ninja-release --target VoxelRenderer --parallel
```

### Web Debug

Run these after activating Emscripten:

```cmd
cmake --preset web-debug
cmake --build --preset web-debug --target VoxelRenderer --parallel
```

### Web Release

```cmd
cmake --preset web-release
cmake --build --preset web-release --target VoxelRenderer --parallel
```

A native `.lib` and a WebAssembly `.a` are different builds. Build the library and your game with the same platform toolchain, architecture, and configuration. The Windows library cannot be linked into an Emscripten game, or the other way around.

## Link it to your CMake game

The simplest option is to keep this repository in your game tree (for example, as `external/VoxelGameEngine`) and add it as a CMake subdirectory. CMake then carries the bgfx, bimg, and bx include and link dependencies through the renderer target for you.

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyGame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Do not build the sample game when using VoxelRenderer as a dependency.
set(VOXEL_BUILD_EXAMPLE OFF)
add_subdirectory(external/VoxelGameEngine)

add_executable(MyGame src/main.cpp)
target_link_libraries(MyGame PRIVATE VoxelRenderer::VoxelRenderer)
```

`target_link_libraries` is the important line. It adds the renderer and its public CMake dependencies to your game. On the first configure, CMake may need internet access to fetch bgfx.cmake. For a browser game, configure the whole parent project with the Emscripten toolchain, then add and link the same target.

### Can I just copy the `.lib`?

The current Windows build is a **static library**. It creates `VoxelRenderer.lib`; it does not create a VoxelRenderer DLL. A static library is linked into your game executable, so you do not copy it beside the finished game.

Copying only `VoxelRenderer.lib` and `include/voxel/Renderer.hpp` is not enough for a manual setup. The public header includes bgfx and bx headers, and VoxelRenderer also links bgfx, bimg, bimg_decode, and bx. A manual MSVC project must use matching dependency headers and `.lib` files for the same compiler, architecture, and Debug/Release configuration. The game also needs the platform system libraries required by the window/backend. This is why linking the CMake target above is the easy path.

A `.dll`-based distribution would also need the DLL at runtime (and its import `.lib` at link time). This project currently produces a static library instead.

## Use the renderer in your game

Your game creates a window or canvas and runs the event loop. Pass that platform's native handle to `Renderer::initialize`; VoxelRenderer does not create windows or read keyboard/mouse input.

The general frame order is:

1. Create your window and initialize the renderer.
2. Create meshes, textures, and materials after the renderer is initialized.
3. Each frame, update your game and camera, call `beginFrame`, draw objects, then call `endFrame`.
4. Reset/destroy materials, textures, and meshes before calling `Renderer::shutdown`.

A compact example of the library API follows. `windowHandle` must come from the windowing code in your game; on Windows that is an `HWND`. The sample's browser platform passes its Emscripten canvas selector.

```cpp
#include <voxel/Renderer.hpp>

#include <array>
#include <string>

// Keep renderer and resources as members of your game/application object.
voxel::Renderer renderer;
voxel::Mesh cube;
voxel::Texture whiteTexture;
voxel::Material material;
voxel::Camera camera;

bool start(void* windowHandle, std::uint32_t width, std::uint32_t height) {
    std::string error;
    if (!renderer.initialize(windowHandle, width, height, BGFX_RESET_VSYNC, &error)) {
        return false; // Log or display `error`.
    }

    cube = voxel::Mesh::makeCube();
    if (!cube.valid() || !whiteTexture.createSolid({}, &error)) {
        return false;
    }

    const std::array<voxel::MaterialUniformDesc, 8> uniforms = {{
        {"s_albedo", bgfx::UniformType::Sampler, 0},
        {"u_uvRect", bgfx::UniformType::Vec4, 0},
        {"u_baseColor", bgfx::UniformType::Vec4, 0},
        {"u_surface", bgfx::UniformType::Vec4, 0},
        {"u_lightDirection", bgfx::UniformType::Vec4, 0},
        {"u_lightColor", bgfx::UniformType::Vec4, 0},
        {"u_ambientColor", bgfx::UniformType::Vec4, 0},
        {"u_cameraPosition", bgfx::UniformType::Vec4, 0}
    }};
    if (!material.load(renderer, uniforms, &error)) {
        return false;
    }

    material.setTexture("s_albedo", whiteTexture);
    material.setVec4("u_uvRect", 0, 0, 1, 1);
    material.setVec4("u_baseColor", 0.8f, 0.25f, 0.12f, 1.0f);
    material.setVec4("u_surface", 24.0f, 0.25f, 0, 0); // shininess, specular strength
    material.setVec4("u_lightDirection", -0.45f, 0.82f, -0.35f, 0);
    material.setVec4("u_lightColor", 1.0f, 0.9f, 0.75f, 1);
    material.setVec4("u_ambientColor", 0.14f, 0.18f, 0.27f, 1);

    camera.position = bx::Vec3{0, 1.8f, -7.5f};
    camera.target = bx::Vec3{0, 0, 0};
    material.setVec4("u_cameraPosition", camera.position.x, camera.position.y, camera.position.z, 1);
    return true;
}

void drawFrame(std::uint16_t width, std::uint16_t height) {
    renderer.beginFrame(camera, width, height, 0x202633ff);

    voxel::Transform transform;
    transform.position = bx::Vec3{0, 0, 0};
    transform.rotation = bx::Vec3{0, 0.5f, 0};
    transform.scale = bx::Vec3{1, 1, 1};
    renderer.draw(cube, material, transform);

    renderer.endFrame();
}

void stop() {
    material.reset();
    whiteTexture.reset();
    cube.reset();
    renderer.shutdown();
}
```

The example omits window creation and event handling on purpose: those belong to your app. For a complete working host app, see `examples/main.cpp` and `examples/platform/`.

## Renderer features

### Camera and transforms

`Camera` contains a world-space position and look-at target, field of view in degrees, and near/far clipping planes. Pass it to `beginFrame` along with the current width and height. The renderer updates bgfx's view and projection when the size changes and clears color and depth each frame.

`Transform` contains position, Euler rotation, and scale. `renderer.draw(mesh, material, transform)` submits one indexed mesh draw. The renderer currently handles one camera/view and does not provide a scene graph or automatic batching.

### Meshes

`Mesh::makeCube()` provides a ready-to-use cube. For your own geometry, create a `std::vector<voxel::Vertex>` and a triangle index list, then call `Mesh::upload(vertices, indices, &error)` after renderer initialization.

Each vertex has a 3D position, normal, and UV coordinate. Indices are 16-bit, so a single mesh supports at most 65,536 vertices. The index list must contain triangles. `Mesh` is move-only; use `valid()` to check it after creation.

### Textures

Load a 2D image from a file:

```cpp
voxel::Texture texture;
std::string error;
if (!texture.load("assets/textures/test.png", &error)) {
    // Report error.
}
```

The path is relative to your program's current working directory unless you pass an absolute path. PNG files are supported by the bundled image decoder. For a browser game, a path must be included in Emscripten's virtual filesystem. You can avoid runtime asset paths by generating or loading your pixels in your app and calling `createRGBA8`.

```cpp
#include <vector>
std::vector<std::uint8_t> pixels(width * height * 4); // RGBA, row-major
voxel::Texture texture;
texture.createRGBA8(width, height, pixels, &error);
```

`createRGBA8` expects exactly `width * height * 4` tightly packed bytes and builds mipmaps during upload. `createSolid` is useful for a 1x1 white texture that lets material color tint an object. Textures are move-only.

### Texture atlas

`TextureAtlas` records named rectangles in a texture; it does not pack images or own the texture. Rectangle coordinates are pixels measured from the image's top-left corner. An inset of one or more texels keeps sampling away from tile edges.

```cpp
voxel::TextureAtlas atlas(texture);
atlas.addRegion("grass", 0, 0, 32, 32, 1);
const voxel::AtlasRegion* grass = atlas.find("grass");
renderer.draw(cube, material, transform, grass);
```

When you pass a region to `draw`, the built-in `u_uvRect` uniform is set to that region's UV rectangle. Keep the texture alive while the material samples it, and keep the atlas alive while you use its region pointers.

### Materials and built-in lighting

A `Material` combines a compiled vertex/fragment shader pair, its declared uniforms, texture bindings, and bgfx render state. The built-in shader implements ambient, one directional diffuse light, and a Blinn-Phong-style specular highlight. It is a simple starter shader, not a physically based rendering model.

Declare every uniform your shader uses when loading the material. The built-in shader uses:

- `s_albedo`: texture sampler, stage 0.
- `u_uvRect`: UV origin and size; atlas draws update it automatically.
- `u_baseColor`: RGBA tint multiplied by the sampled texture.
- `u_surface`: `x` is shininess and `y` is specular strength.
- `u_lightDirection`: direction toward the light.
- `u_lightColor`: directional light color.
- `u_ambientColor`: ambient light color.
- `u_cameraPosition`: world-space camera position for the specular highlight.

The `Material::load(renderer, uniforms, &error)` overload loads the built-in shader pair. Copying a material shares its compiled shader program but keeps that copy's uniform and texture values independent, which is useful for making red, blue, and gold variants from one shader.

`setVec4` changes a declared vec4 uniform. `setTexture` assigns a declared sampler. The current material API supports vec4 and sampler uniforms; sampler stages range from 0 to 15. `setRenderState` accepts bgfx state flags. The default writes color and depth, tests depth, and culls counter-clockwise faces.

### Custom shaders

Shader source uses bgfx's `.sc` format. Provide a matching `vs_*.sc` and `fs_*.sc` pair. Compilation uses the shared `assets/shaders/varying.def.sc` file; add any new varying declarations there before rebuilding. The renderer's vertex layout supplies position, normal, and UV attributes; uniforms available through `Material` must be vec4 values or samplers.

To compile built-in and game-owned shaders with bgfx `shaderc`, configure a native MSVC build like this before adding the renderer:

```cmake
set(VOXEL_BUILD_EXAMPLE OFF)
set(VOXEL_REBUILD_SHADERS ON)
set(VOXEL_ADDITIONAL_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
add_subdirectory(external/VoxelGameEngine)
```

Build the native `VoxelRenderer` target first. This compiles D3D11 and WebGL 2 shader binaries and embeds them in that library build. Then configure/build the Emscripten version; it embeds the ESSL binaries generated by the native shader compilation. Give your material the same uniform names and types as the shader:

```cpp
material.load(renderer, "vs_custom.bin", "fs_custom.bin", uniforms, &error);
```

Binaries compiled and included this way are embedded; they do not need to be copied beside the game. A shader filename that is not embedded is treated as a runtime file path. Shader names must be unique across the built-in and additional shader folders.

## Project map

- `include/voxel/Renderer.hpp` — public renderer API.
- `src/Renderer.cpp` — bgfx resource creation and draw submission.
- `assets/shaders/` — built-in shader source, varying declarations, and precompiled DX11/ESSL binaries.
- `examples/` — sample game and its Windows/Emscripten window and input code.
- `cmake/EmbedShaders.cmake` — embeds compiled shader binaries into the library during the build.
- `CMakePresets.json` — standard Debug/Release and platform build configurations.

The renderer does not currently include window/input handling, automatic atlas packing, model-file import, a scene graph, physics, or audio. Add those systems in your game or another library.






