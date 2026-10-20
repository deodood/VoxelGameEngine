#include "voxel/Renderer.hpp"

#include "voxel/EmbeddedShaders.hpp"

#include <bimg/decode.h>
#include <bx/allocator.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace voxel {
namespace {

bool bgfxIsInitialized() noexcept {
    return bgfx::getRendererType() != bgfx::RendererType::Noop;
}

void setError(std::string* output, const std::string& message) {
    if (output != nullptr) {
        *output = message;
    }
    std::cerr << "[VoxelRenderer] " << message << '\n';
}

bool readFile(const std::string& path, std::vector<std::uint8_t>& bytes, std::string* error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        setError(error, "Could not open file: " + path);
        return false;
    }

    const std::streamoff end = file.tellg();
    if (end <= 0 || static_cast<std::uint64_t>(end) > std::numeric_limits<std::uint32_t>::max()) {
        setError(error, "File is empty or too large: " + path);
        return false;
    }

    bytes.resize(static_cast<std::size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        setError(error, "Could not read file: " + path);
        return false;
    }
    return true;
}

bgfx::ShaderHandle createShader(std::span<const std::uint8_t> bytes,
                                const char* name,
                                std::string* error) {
    if (bytes.empty()) {
        setError(error, std::string("Shader binary is empty: ") + name);
        return BGFX_INVALID_HANDLE;
    }
    bgfx::ShaderHandle shader = bgfx::createShader(
        bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size())));
    if (bgfx::isValid(shader)) {
        bgfx::setName(shader, name);
    } else {
        setError(error, std::string("bgfx could not create shader from: ") + name);
    }
    return shader;
}

bgfx::ShaderHandle loadShader(const std::string& path, std::string* error) {
    std::vector<std::uint8_t> bytes;
    if (!readFile(path, bytes, error)) {
        return BGFX_INVALID_HANDLE;
    }
    return createShader(bytes, path.c_str(), error);
}

const char* shaderBackendDirectory(bgfx::RendererType::Enum renderer) noexcept {
    switch (renderer) {
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return "dx11";
    case bgfx::RendererType::OpenGLES:
        return "essl";
    case bgfx::RendererType::OpenGL:
        return "glsl";
    case bgfx::RendererType::Vulkan:
        return "spirv";
    case bgfx::RendererType::Metal:
        return "metal";
    default:
        return nullptr;
    }
}

} // namespace

void Transform::toMatrix(float output[16]) const noexcept {
    bx::mtxSRT(output,
               scale.x, scale.y, scale.z,
               rotation.x, rotation.y, rotation.z,
               position.x, position.y, position.z);
}

Mesh::~Mesh() { reset(); }

Mesh::Mesh(Mesh&& other) noexcept
    : vertexBuffer_(other.vertexBuffer_), indexBuffer_(other.indexBuffer_), indexCount_(other.indexCount_) {
    other.vertexBuffer_ = BGFX_INVALID_HANDLE;
    other.indexBuffer_ = BGFX_INVALID_HANDLE;
    other.indexCount_ = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        reset();
        vertexBuffer_ = other.vertexBuffer_;
        indexBuffer_ = other.indexBuffer_;
        indexCount_ = other.indexCount_;
        other.vertexBuffer_ = BGFX_INVALID_HANDLE;
        other.indexBuffer_ = BGFX_INVALID_HANDLE;
        other.indexCount_ = 0;
    }
    return *this;
}

bool Mesh::upload(std::span<const Vertex> vertices,
                  std::span<const std::uint16_t> indices,
                  std::string* error) {
    reset();
    if (!bgfxIsInitialized()) {
        setError(error, "Upload meshes after Renderer::initialize has initialized bgfx.");
        return false;
    }
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0 ||
        vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1) {
        setError(error, "A mesh needs vertices and a triangle index list supported by 16-bit indices.");
        return false;
    }
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max() / sizeof(Vertex) ||
        indices.size() > std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint16_t)) {
        setError(error, "Mesh data is too large for a single bgfx buffer.");
        return false;
    }
    for (const std::uint16_t index : indices) {
        if (index >= vertices.size()) {
            setError(error, "Mesh index points past the end of the vertex array.");
            return false;
        }
    }

    static_assert(sizeof(Vertex) == sizeof(float) * 8, "Vertex data must remain tightly packed.");
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();

    vertexBuffer_ = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size_bytes())), layout);
    indexBuffer_ = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(), static_cast<std::uint32_t>(indices.size_bytes())));
    if (!bgfx::isValid(vertexBuffer_) || !bgfx::isValid(indexBuffer_)) {
        reset();
        setError(error, "bgfx could not allocate the mesh buffers.");
        return false;
    }
    indexCount_ = static_cast<std::uint32_t>(indices.size());
    return true;
}

Mesh Mesh::makeCube() {
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    struct Face {
        float normal[3];
        float positions[4][3];
    };
    const Face faces[] = {
        {{0, 0, 1},  {{-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}}},
        {{0, 0, -1}, {{ 0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}}},
        {{1, 0, 0},  {{ 0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f,  0.5f}}},
        {{-1, 0, 0}, {{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f, -0.5f}}},
        {{0, 1, 0},  {{-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}}},
        {{0, -1, 0}, {{-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f}}}
    };

    std::array<Vertex, 24> vertices{};
    std::array<std::uint16_t, 36> indices{};
    for (std::size_t faceIndex = 0; faceIndex < std::size(faces); ++faceIndex) {
        for (std::size_t vertexIndex = 0; vertexIndex < 4; ++vertexIndex) {
            Vertex& vertex = vertices[faceIndex * 4 + vertexIndex];
            std::copy(std::begin(faces[faceIndex].positions[vertexIndex]),
                      std::end(faces[faceIndex].positions[vertexIndex]), vertex.position);
            std::copy(std::begin(faces[faceIndex].normal), std::end(faces[faceIndex].normal), vertex.normal);
            std::copy(std::begin(uv[vertexIndex]), std::end(uv[vertexIndex]), vertex.uv);
        }
        const std::uint16_t base = static_cast<std::uint16_t>(faceIndex * 4);
        const std::size_t offset = faceIndex * 6;
        indices[offset + 0] = static_cast<std::uint16_t>(base + 0);
        indices[offset + 1] = static_cast<std::uint16_t>(base + 1);
        indices[offset + 2] = static_cast<std::uint16_t>(base + 2);
        indices[offset + 3] = static_cast<std::uint16_t>(base + 0);
        indices[offset + 4] = static_cast<std::uint16_t>(base + 2);
        indices[offset + 5] = static_cast<std::uint16_t>(base + 3);
    }

    Mesh mesh;
    mesh.upload(vertices, indices);
    return mesh;
}

bool Mesh::valid() const noexcept {
    return bgfx::isValid(vertexBuffer_) && bgfx::isValid(indexBuffer_);
}

std::uint32_t Mesh::indexCount() const noexcept { return indexCount_; }

void Mesh::reset() noexcept {
    if (bgfxIsInitialized()) {
        if (bgfx::isValid(vertexBuffer_)) {
            bgfx::destroy(vertexBuffer_);
        }
        if (bgfx::isValid(indexBuffer_)) {
            bgfx::destroy(indexBuffer_);
        }
    }
    vertexBuffer_ = BGFX_INVALID_HANDLE;
    indexBuffer_ = BGFX_INVALID_HANDLE;
    indexCount_ = 0;
}

Texture::~Texture() { reset(); }

Texture::Texture(Texture&& other) noexcept
    : handle_(other.handle_), width_(other.width_), height_(other.height_) {
    other.handle_ = BGFX_INVALID_HANDLE;
    other.width_ = 0;
    other.height_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        reset();
        handle_ = other.handle_;
        width_ = other.width_;
        height_ = other.height_;
        other.handle_ = BGFX_INVALID_HANDLE;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

bool Texture::load(const std::string& path, std::string* error) {
    reset();
    if (!bgfxIsInitialized()) {
        setError(error, "Load textures after Renderer::initialize has initialized bgfx.");
        return false;
    }

    std::vector<std::uint8_t> encoded;
    if (!readFile(path, encoded, error)) {
        return false;
    }
    bx::DefaultAllocator allocator;
    bimg::ImageContainer* image = bimg::imageParse(
        &allocator, encoded.data(), static_cast<std::uint32_t>(encoded.size()), bimg::TextureFormat::RGBA8);
    if (image == nullptr) {
        setError(error, "The image could not be decoded as RGBA8: " + path);
        return false;
    }
    if (image->m_width == 0 || image->m_height == 0 || image->m_depth > 1 || image->m_cubeMap ||
        image->m_width > std::numeric_limits<std::uint16_t>::max() ||
        image->m_height > std::numeric_limits<std::uint16_t>::max()) {
        bimg::imageFree(image);
        setError(error, "Texture must be a 2D image between 1 and 65535 pixels per side: " + path);
        return false;
    }

    const std::uint16_t width = static_cast<std::uint16_t>(image->m_width);
    const std::uint16_t height = static_cast<std::uint16_t>(image->m_height);
    const std::uint64_t baseLevelSize = static_cast<std::uint64_t>(width) * height * 4;
    if (image->m_size < baseLevelSize) {
        bimg::imageFree(image);
        setError(error, "Decoded texture is smaller than its RGBA8 base level: " + path);
        return false;
    }
    const auto* pixels = static_cast<const std::uint8_t*>(image->m_data);
    const bool uploaded = createRGBA8(
        width, height, std::span<const std::uint8_t>(pixels, static_cast<std::size_t>(baseLevelSize)), error);
    bimg::imageFree(image);
    if (uploaded) {
        bgfx::setName(handle_, path.c_str());
    }
    return uploaded;
}

bool Texture::createRGBA8(std::uint16_t width,
                          std::uint16_t height,
                          std::span<const std::uint8_t> pixels,
                          std::string* error) {
    reset();
    if (!bgfxIsInitialized()) {
        setError(error, "Create textures after Renderer::initialize has initialized bgfx.");
        return false;
    }
    const std::uint64_t baseLevelSize = static_cast<std::uint64_t>(width) * height * 4;
    if (width == 0 || height == 0 || pixels.size() != baseLevelSize ||
        baseLevelSize > std::numeric_limits<std::uint32_t>::max()) {
        setError(error, "RGBA8 texture data must contain exactly width * height * 4 bytes.");
        return false;
    }

    std::vector<std::uint8_t> mipData(pixels.begin(), pixels.end());
    std::uint32_t currentWidth = width;
    std::uint32_t currentHeight = height;
    bool hasMipmaps = false;
    while (currentWidth > 1 || currentHeight > 1) {
        const std::uint32_t nextWidth = std::max<std::uint32_t>(1, currentWidth / 2);
        const std::uint32_t nextHeight = std::max<std::uint32_t>(1, currentHeight / 2);
        const std::size_t sourceOffset = mipData.size() -
            static_cast<std::size_t>(currentWidth) * currentHeight * 4;
        const std::uint8_t* source = mipData.data() + sourceOffset;
        std::vector<std::uint8_t> nextLevel(static_cast<std::size_t>(nextWidth) * nextHeight * 4);
        for (std::uint32_t y = 0; y < nextHeight; ++y) {
            const std::uint32_t y0 = y * currentHeight / nextHeight;
            const std::uint32_t y1 = (y + 1) * currentHeight / nextHeight;
            for (std::uint32_t x = 0; x < nextWidth; ++x) {
                const std::uint32_t x0 = x * currentWidth / nextWidth;
                const std::uint32_t x1 = (x + 1) * currentWidth / nextWidth;
                std::uint32_t sums[4] = {0, 0, 0, 0};
                std::uint32_t samples = 0;
                for (std::uint32_t sourceY = y0; sourceY < y1; ++sourceY) {
                    for (std::uint32_t sourceX = x0; sourceX < x1; ++sourceX) {
                        const std::size_t sourcePixel =
                            (static_cast<std::size_t>(sourceY) * currentWidth + sourceX) * 4;
                        for (std::size_t channel = 0; channel < 4; ++channel) {
                            sums[channel] += source[sourcePixel + channel];
                        }
                        ++samples;
                    }
                }
                const std::size_t destinationPixel =
                    (static_cast<std::size_t>(y) * nextWidth + x) * 4;
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    nextLevel[destinationPixel + channel] =
                        static_cast<std::uint8_t>(sums[channel] / samples);
                }
            }
        }
        if (mipData.size() + nextLevel.size() > std::numeric_limits<std::uint32_t>::max()) {
            setError(error, "Texture mip data is too large for a bgfx upload.");
            return false;
        }
        mipData.insert(mipData.end(), nextLevel.begin(), nextLevel.end());
        currentWidth = nextWidth;
        currentHeight = nextHeight;
        hasMipmaps = true;
    }

    if (mipData.size() > std::numeric_limits<std::uint32_t>::max()) {
        setError(error, "Texture mip data is too large for a bgfx upload.");
        return false;
    }
    handle_ = bgfx::createTexture2D(width, height, hasMipmaps, 1, bgfx::TextureFormat::RGBA8,
                                    BGFX_TEXTURE_NONE,
                                    bgfx::copy(mipData.data(), static_cast<std::uint32_t>(mipData.size())));
    if (!bgfx::isValid(handle_)) {
        setError(error, "bgfx could not create the RGBA8 texture.");
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

bool Texture::createSolid(Color color, std::string* error) {
    reset();
    if (!bgfxIsInitialized()) {
        setError(error, "Create textures after Renderer::initialize has initialized bgfx.");
        return false;
    }
    const auto channel = [](float value) {
        return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    const std::array<std::uint8_t, 4> pixel = {channel(color.r), channel(color.g), channel(color.b), channel(color.a)};
    handle_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                    bgfx::copy(pixel.data(), static_cast<std::uint32_t>(pixel.size())));
    if (!bgfx::isValid(handle_)) {
        setError(error, "bgfx could not create a solid texture.");
        return false;
    }
    width_ = height_ = 1;
    return true;
}

bool Texture::valid() const noexcept { return bgfx::isValid(handle_); }
std::uint16_t Texture::width() const noexcept { return width_; }
std::uint16_t Texture::height() const noexcept { return height_; }

void Texture::reset() noexcept {
    if (bgfxIsInitialized() && bgfx::isValid(handle_)) {
        bgfx::destroy(handle_);
    }
    handle_ = BGFX_INVALID_HANDLE;
    width_ = 0;
    height_ = 0;
}

TextureAtlas::TextureAtlas(const Texture& texture) noexcept
    : textureWidth_(texture.width()), textureHeight_(texture.height()) {}

bool TextureAtlas::addRegion(std::string name,
                             std::uint16_t x,
                             std::uint16_t y,
                             std::uint16_t width,
                             std::uint16_t height,
                             std::uint16_t insetTexels) {
    if (name.empty() || textureWidth_ == 0 || textureHeight_ == 0 || width == 0 || height == 0 ||
        static_cast<std::uint32_t>(x) + width > textureWidth_ ||
        static_cast<std::uint32_t>(y) + height > textureHeight_ ||
        static_cast<std::uint32_t>(insetTexels) * 2 >= width ||
        static_cast<std::uint32_t>(insetTexels) * 2 >= height) {
        return false;
    }

    const float inset = static_cast<float>(insetTexels) + 0.5f;
    AtlasRegion region;
    region.u = (static_cast<float>(x) + inset) / static_cast<float>(textureWidth_);
    region.v = (static_cast<float>(y) + inset) / static_cast<float>(textureHeight_);
    region.width = (static_cast<float>(width) - inset * 2.0f) / static_cast<float>(textureWidth_);
    region.height = (static_cast<float>(height) - inset * 2.0f) / static_cast<float>(textureHeight_);
    const auto found = std::find_if(regions_.begin(), regions_.end(), [&](const auto& entry) {
        return entry.first == name;
    });
    if (found != regions_.end()) {
        found->second = region;
    } else {
        regions_.emplace_back(std::move(name), region);
    }
    return true;
}

const AtlasRegion* TextureAtlas::find(std::string_view name) const noexcept {
    const auto found = std::find_if(regions_.begin(), regions_.end(), [&](const auto& entry) {
        return entry.first == name;
    });
    return found == regions_.end() ? nullptr : &found->second;
}

struct Material::Program {
    struct Binding {
        std::string name;
        bgfx::UniformType::Enum type = bgfx::UniformType::Vec4;
        std::uint8_t stage = 0;
        bgfx::UniformHandle handle = BGFX_INVALID_HANDLE;
    };

    bgfx::ProgramHandle handle = BGFX_INVALID_HANDLE;
    std::vector<Binding> bindings;

    ~Program() {
        if (!bgfxIsInitialized()) {
            return;
        }
        if (bgfx::isValid(handle)) {
            bgfx::destroy(handle);
        }
        for (const Binding& binding : bindings) {
            if (bgfx::isValid(binding.handle)) {
                bgfx::destroy(binding.handle);
            }
        }
    }
};

Material::Material() = default;
Material::~Material() = default;
Material::Material(const Material& other) = default;
Material& Material::operator=(const Material& other) = default;
Material::Material(Material&& other) noexcept = default;
Material& Material::operator=(Material&& other) noexcept = default;

bool Material::load(Renderer& renderer,
                    std::string_view vertexShaderFile,
                    std::string_view fragmentShaderFile,
                    std::span<const MaterialUniformDesc> uniforms,
                    std::string* error) {
    reset();
    if (!renderer.ready()) {
        setError(error, "Load materials after Renderer::initialize.");
        return false;
    }

    const auto loadMaterialShader = [&](std::string_view filename) {
        const std::string shaderName(filename);
        const std::span<const std::uint8_t> embedded =
            detail::findBuiltinShader(renderer.backendDirectory_, filename);
        if (!embedded.empty()) {
            return createShader(embedded, shaderName.c_str(), error);
        }
        return loadShader(shaderName, error);
    };

    const bgfx::ShaderHandle vertexShader = loadMaterialShader(vertexShaderFile);
    if (!bgfx::isValid(vertexShader)) {
        return false;
    }
    const bgfx::ShaderHandle fragmentShader = loadMaterialShader(fragmentShaderFile);
    if (!bgfx::isValid(fragmentShader)) {
        bgfx::destroy(vertexShader);
        return false;
    }

    auto program = std::make_shared<Program>();
    program->handle = bgfx::createProgram(vertexShader, fragmentShader, true);
    if (!bgfx::isValid(program->handle)) {
        bgfx::destroy(vertexShader);
        bgfx::destroy(fragmentShader);
        setError(error, "bgfx could not link the material shader program.");
        return false;
    }

    for (const MaterialUniformDesc& desc : uniforms) {
        if (desc.name.empty() ||
            (desc.type != bgfx::UniformType::Vec4 && desc.type != bgfx::UniformType::Sampler) ||
            (desc.type == bgfx::UniformType::Sampler && desc.textureStage >= 16)) {
            setError(error, "Material uniforms must be named vec4 or sampler uniforms (sampler stages 0-15).");
            return false;
        }
        const auto duplicate = std::find_if(program->bindings.begin(), program->bindings.end(), [&](const auto& b) {
            return b.name == desc.name;
        });
        if (duplicate != program->bindings.end()) {
            setError(error, "Duplicate material uniform: " + desc.name);
            return false;
        }
        Program::Binding binding;
        binding.name = desc.name;
        binding.type = desc.type;
        binding.stage = desc.textureStage;
        binding.handle = bgfx::createUniform(binding.name.c_str(), binding.type);
        if (!bgfx::isValid(binding.handle)) {
            setError(error, "bgfx could not create material uniform: " + binding.name);
            return false;
        }
        program->bindings.push_back(std::move(binding));
    }

    program_ = std::move(program);
    values_.resize(program_->bindings.size());
    renderState_ = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                   BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW;
    return true;
}

bool Material::load(Renderer& renderer,
                    std::span<const MaterialUniformDesc> uniforms,
                    std::string* error) {
    return load(renderer, "vs_material.bin", "fs_material.bin", uniforms, error);
}

bool Material::setVec4(std::string_view name, float x, float y, float z, float w) noexcept {
    return setVec4(name, std::array<float, 4>{x, y, z, w});
}

bool Material::setVec4(std::string_view name, const std::array<float, 4>& value) noexcept {
    if (!program_) {
        return false;
    }
    for (std::size_t index = 0; index < program_->bindings.size(); ++index) {
        const Program::Binding& binding = program_->bindings[index];
        if (binding.name == name && binding.type == bgfx::UniformType::Vec4) {
            values_[index].vector = value;
            values_[index].assigned = true;
            return true;
        }
    }
    return false;
}

bool Material::setTexture(std::string_view name, const Texture& texture, std::uint32_t samplerFlags) noexcept {
    if (!program_ || !texture.valid()) {
        return false;
    }
    for (std::size_t index = 0; index < program_->bindings.size(); ++index) {
        const Program::Binding& binding = program_->bindings[index];
        if (binding.name == name && binding.type == bgfx::UniformType::Sampler) {
            values_[index].texture = texture.handle_;
            values_[index].samplerFlags = samplerFlags;
            values_[index].assigned = true;
            return true;
        }
    }
    return false;
}

void Material::setRenderState(std::uint64_t state) noexcept { renderState_ = state; }
bool Material::valid() const noexcept { return program_ && bgfx::isValid(program_->handle); }

void Material::reset() noexcept {
    values_.clear();
    program_.reset();
}

Renderer::~Renderer() { shutdown(); }

bool Renderer::initialize(void* nativeWindowHandle,
                          std::uint32_t width,
                          std::uint32_t height,
                          std::uint32_t resetFlags,
                          std::string* error) {
    shutdown();
    if (nativeWindowHandle == nullptr || width == 0 || height == 0) {
        setError(error, "Renderer needs a native window/canvas handle and a non-zero size.");
        return false;
    }

    bgfx::Init init;
#if defined(__EMSCRIPTEN__)
    init.type = bgfx::RendererType::OpenGLES;
#elif defined(_WIN32)
    init.type = bgfx::RendererType::Direct3D11;
#else
    init.type = bgfx::RendererType::Count;
#endif
    init.platformData.nwh = nativeWindowHandle;
    init.resolution.width = width;
    init.resolution.height = height;
    init.resolution.reset = resetFlags;
    if (!bgfx::init(init)) {
        setError(error, "bgfx initialization failed. Check that the graphics backend is available.");
        return false;
    }

    const char* backend = shaderBackendDirectory(bgfx::getRendererType());
    if (backend == nullptr) {
        setError(error, "No shader binary directory is available for the selected bgfx renderer.");
        bgfx::shutdown();
        return false;
    }
    initialized_ = true;
    backendDirectory_ = backend;
    width_ = static_cast<std::uint16_t>(std::clamp<std::uint32_t>(width, 1, 65535));
    height_ = static_cast<std::uint16_t>(std::clamp<std::uint32_t>(height, 1, 65535));
    resetFlags_ = resetFlags;
    return true;
}

void Renderer::shutdown() noexcept {
    if (initialized_) {
        bgfx::shutdown();
        initialized_ = false;
    }
    backendDirectory_.clear();
    width_ = 0;
    height_ = 0;
}

void Renderer::beginFrame(const Camera& camera,
                          std::uint16_t width,
                          std::uint16_t height,
                          std::uint32_t clearColor) {
    if (!ready() || width == 0 || height == 0) {
        return;
    }
    if (width != width_ || height != height_) {
        bgfx::reset(width, height, resetFlags_);
        width_ = width;
        height_ = height;
    }
    bgfx::setViewRect(0, 0, 0, width, height);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColor, 1.0f, 0);
    float view[16];
    bx::mtxLookAt(view, camera.position, camera.target);
    float projection[16];
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    bx::mtxProj(projection, camera.fieldOfViewDegrees, aspect, camera.nearPlane, camera.farPlane,
                bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(0, view, projection);
    bgfx::touch(0);
}

void Renderer::draw(const Mesh& mesh,
                    const Material& material,
                    const Transform& transform,
                    const AtlasRegion* region) {
    if (!ready() || !mesh.valid() || !material.valid()) {
        return;
    }
    bool missingTexture = false;
    const std::array<float, 4> regionUv = region != nullptr
        ? std::array<float, 4>{region->u, region->v, region->width, region->height}
        : std::array<float, 4>{0.0f, 0.0f, 1.0f, 1.0f};
    for (std::size_t index = 0; index < material.program_->bindings.size(); ++index) {
        const Material::Program::Binding& binding = material.program_->bindings[index];
        const Material::Value& value = material.values_[index];
        if (binding.type == bgfx::UniformType::Sampler) {
            if (!value.assigned || !bgfx::isValid(value.texture)) {
                missingTexture = true;
                break;
            }
            bgfx::setTexture(binding.stage, binding.handle, value.texture, value.samplerFlags);
        } else if (binding.name == "u_uvRect" && region != nullptr) {
            bgfx::setUniform(binding.handle, regionUv.data());
        } else if (value.assigned) {
            bgfx::setUniform(binding.handle, value.vector.data());
        }
    }
    if (missingTexture) {
        return;
    }

    float model[16];
    transform.toMatrix(model);
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, mesh.vertexBuffer_);
    bgfx::setIndexBuffer(mesh.indexBuffer_);
    bgfx::setState(material.renderState_);
    bgfx::submit(0, material.program_->handle);
}

void Renderer::endFrame() {
    if (ready()) {
        bgfx::frame();
    }
}

bool Renderer::ready() const noexcept { return initialized_; }

} // namespace voxel
