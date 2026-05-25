#pragma once

#include "lute/ui/Profile.h"
#include "lute/ui/Render.h"
#include "lute/ui/Text.h"

#if LUTE_UI_USE_DAWN
#include <webgpu/webgpu_cpp.h>
#endif

#if LUTE_UI_USE_HARFBUZZ_GPU
#include "hb.h"
#include "hb-gpu.h"
#include "hb-ot.h"
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lute::ui::dawn
{

#if LUTE_UI_USE_DAWN

constexpr uint64_t kAtlasCapacity = 256 * 1024;
constexpr uint32_t kImageAtlasSize = 2048;
constexpr uint32_t kImageAtlasMipLevels = 4;
constexpr uint32_t kImageAtlasGutter = 8;
constexpr uint32_t kImageAtlasAlignment = 1u << (kImageAtlasMipLevels - 1);

struct AdapterRequest
{
    wgpu::Adapter adapter;
    bool ok = false;
};

struct DeviceRequest
{
    wgpu::Device device;
    bool ok = false;
};

struct SurfaceUniforms
{
    float viewport[2] = {};
    float padding[2] = {};
};

struct TextUniforms
{
    float mvp[16] = {};
    float viewport[2] = {};
    float stemDarkening = 1.0f;
    float debug = 0.0f;
};

struct SolidVertex
{
    float position[2] = {};
    float local[2] = {};
    float rect[4] = {};
    float color[4] = {};
};

struct GlyphVertex
{
    float position[2] = {};
    float texcoord[2] = {};
    float normal[2] = {};
    float emPerPos = 0.0f;
    uint32_t atlasOffset = 0;
    float color[4] = {};
    float background[4] = {0.0f, 0.0f, 0.0f, -1.0f};
};

struct ImageGlyphVertex
{
    float position[2] = {};
    float texcoord[2] = {};
};

struct EncodedGlyph
{
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    uint32_t atlasOffset = 0;
    uint32_t renderMode = 0;
    bool empty = true;
};

struct ImageGlyph
{
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    bool empty = true;
};

struct DecodedImage
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

enum class ImageGlyphBlobFormat
{
    Unknown,
    Png,
    Svg,
};

struct ImageGlyphDecodeRequest
{
    std::string_view bytes;
    ImageGlyphBlobFormat format = ImageGlyphBlobFormat::Unknown;
    uint32_t maxWidth = kImageAtlasSize - 2 * kImageAtlasGutter;
    uint32_t maxHeight = kImageAtlasSize - 2 * kImageAtlasGutter;
};

class ImageGlyphDecoder
{
public:
    virtual ~ImageGlyphDecoder() = default;
    virtual std::optional<DecodedImage> decode(const ImageGlyphDecodeRequest& request) = 0;
};

struct ImageGlyphAtlasSlot
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t uploadX = 0;
    uint32_t uploadY = 0;
    uint32_t uploadWidth = 0;
    uint32_t uploadHeight = 0;
};

struct FrameVertices
{
    std::vector<SolidVertex> solid;
    std::vector<GlyphVertex> drawGlyphs;
    std::vector<GlyphVertex> paintGlyphs;
    std::vector<ImageGlyphVertex> imageGlyphs;
};

struct GlyphCacheKey
{
    const FontFace* fontFace = nullptr;
    uint32_t glyph = 0;
    uint32_t variant = 0;

    bool operator==(const GlyphCacheKey& other) const
    {
        return fontFace == other.fontFace && glyph == other.glyph && variant == other.variant;
    }
};

struct GlyphCacheKeyHash
{
    size_t operator()(const GlyphCacheKey& key) const
    {
        return (reinterpret_cast<uintptr_t>(key.fontFace) >> 4) ^ (static_cast<size_t>(key.glyph) * 0x9e3779b1u) ^
            (static_cast<size_t>(key.variant) * 0x85ebca6bu);
    }
};

uint64_t alignTo(uint64_t value, uint64_t alignment);
std::array<float, 4> colorToLinearFloat(Color color);
wgpu::Color colorToLinearDawnColor(Color color);
wgpu::TextureFormat chooseSurfaceFormat(const wgpu::SurfaceCapabilities& capabilities);
wgpu::ShaderModule createShaderModule(const wgpu::Device& device, const std::string& source);
wgpu::Buffer createBuffer(const wgpu::Device& device, uint64_t size, wgpu::BufferUsage usage);
bool isGoodSurfaceTexture(wgpu::SurfaceGetCurrentTextureStatus status);
wgpu::PresentMode choosePresentMode(const wgpu::SurfaceCapabilities& capabilities);
wgpu::CompositeAlphaMode chooseAlphaMode(const wgpu::SurfaceCapabilities& capabilities);

void appendSolidRect(std::vector<SolidVertex>& vertices, Rect rect, float radius, Color color, float scale);
void appendGlyphVertices(
    std::vector<GlyphVertex>& vertices,
    float x,
    float y,
    float fontSize,
    uint32_t upem,
    const EncodedGlyph& glyph,
    const std::array<float, 4>& color,
    const std::array<float, 4>& background
);
void appendImageGlyphVertices(std::vector<ImageGlyphVertex>& vertices, float x, float y, float fontSize, uint32_t upem, const ImageGlyph& glyph);

#if LUTE_UI_USE_HARFBUZZ_GPU
ImageGlyphDecoder& imageGlyphDecoder();
std::optional<DecodedImage> decodeImageGlyphBlob(hb_blob_t* blob, ImageGlyphBlobFormat format);
#endif

class DawnBackend
{
public:
    ~DawnBackend();

    bool renderScene(const Scene& scene);
    bool renderToMetalLayer(const Scene& scene, void* metalLayer, uint32_t pixelWidth, uint32_t pixelHeight, float scale);

private:
    bool ensureInstance();
    bool ensureDevice(bool requireMetal, const wgpu::Surface* compatibleSurface);
    void resetDeviceObjects(bool clearSurface);
    bool ensureSurfaceObject(void* metalLayer);
    bool configureSurface(uint32_t pixelWidth, uint32_t pixelHeight);

    bool renderIntoView(const wgpu::TextureView& view, wgpu::TextureFormat format, const Scene& scene, uint32_t width, uint32_t height, float scale);
    void buildSceneVertices(const Scene& scene, FrameVertices& vertices, float scale);
    void appendTextRun(FrameVertices& vertices, const DisplayItem& item, float scale);
    void setPixelMvp(TextUniforms& uniforms, uint32_t width, uint32_t height);

    bool ensurePipelines(wgpu::TextureFormat format);
    bool ensureSolidPipeline(wgpu::TextureFormat format);
    bool ensureTextPipeline(wgpu::TextureFormat format);
    bool ensureImageGlyphPipeline(wgpu::TextureFormat format);
    bool ensureImageGlyphAtlasResources();

    template<typename Vertex>
    void writeVertexData(wgpu::Buffer& buffer, uint64_t& capacity, const std::vector<Vertex>& vertices)
    {
        if (vertices.empty())
            return;

        uint64_t byteSize = vertices.size() * sizeof(Vertex);
        if (!buffer || capacity < byteSize)
        {
            capacity = alignTo(std::max<uint64_t>(byteSize, 4096), 4096);
            buffer = createBuffer(device, capacity, wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst);
        }

        {
            ProfileZone zone(ProfilePhase::Upload);
            queue.WriteBuffer(buffer, 0, vertices.data(), byteSize);
            UiProfiler::addBufferUpload(byteSize);
        }
    }

#if LUTE_UI_USE_HARFBUZZ_GPU
    bool ensureGlyphEncoders();
    const EncodedGlyph* lookupVectorGlyph(const FontFace& fontFace, hb_codepoint_t glyph);
    bool tryEncodePaintGlyph(const FontFace& fontFace, hb_codepoint_t glyph, EncodedGlyph& result);
    bool tryEncodeDrawGlyph(const FontFace& fontFace, hb_codepoint_t glyph, EncodedGlyph& result);
    bool finishEncodedGlyph(hb_blob_t* encoded, const hb_glyph_extents_t& extents, uint32_t renderMode, EncodedGlyph& result);
    bool appendAtlas(const char* data, unsigned int length, uint32_t& offset);
    void markAtlasDirtyRange(uint64_t offset, uint64_t texels);
    void clearAtlasDirtyRange();

    const ImageGlyph* lookupImageGlyph(const FontFace& fontFace, hb_codepoint_t glyph, uint32_t targetPpem);
    ImageGlyph uploadImageGlyph(const FontFace& fontFace, hb_codepoint_t glyph, const DecodedImage& image);
    bool allocateImageAtlas(uint32_t width, uint32_t height, ImageGlyphAtlasSlot& slot);
    bool uploadImageGlyphMipChain(const ImageGlyphAtlasSlot& slot, const DecodedImage& image);
    bool resetImageGlyphAtlas();
#endif

    bool ready = false;
    bool metalDevice = false;
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;

    void* surfaceLayer = nullptr;
    wgpu::Surface surface;
    wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
    uint32_t surfaceWidth = 0;
    uint32_t surfaceHeight = 0;
    bool surfaceConfigured = false;

    wgpu::TextureFormat pipelineFormat = wgpu::TextureFormat::Undefined;
    wgpu::RenderPipeline solidPipeline;
    wgpu::RenderPipeline textPipeline;
    wgpu::RenderPipeline paintTextPipeline;
    wgpu::RenderPipeline imageGlyphPipeline;
    wgpu::BindGroupLayout surfaceBindGroupLayout;
    wgpu::BindGroupLayout textBindGroupLayout;
    wgpu::BindGroupLayout imageGlyphBindGroupLayout;
    wgpu::BindGroup surfaceBindGroup;
    wgpu::BindGroup textBindGroup;
    wgpu::BindGroup imageGlyphBindGroup;
    wgpu::Buffer surfaceUniformBuffer;
    wgpu::Buffer textUniformBuffer;
    wgpu::Buffer solidVertexBuffer;
    wgpu::Buffer glyphVertexBuffer;
    wgpu::Buffer paintGlyphVertexBuffer;
    wgpu::Buffer imageGlyphVertexBuffer;
    wgpu::Buffer atlasBuffer;
    wgpu::Texture imageAtlasTexture;
    wgpu::TextureView imageAtlasTextureView;
    wgpu::Sampler imageAtlasSampler;
    uint64_t solidVertexCapacity = 0;
    uint64_t glyphVertexCapacity = 0;
    uint64_t paintGlyphVertexCapacity = 0;
    uint64_t imageGlyphVertexCapacity = 0;

    std::vector<int32_t> atlasShadow;
    uint64_t atlasCursor = 0;
    uint64_t atlasDirtyStart = kAtlasCapacity;
    uint64_t atlasDirtyEnd = 0;
    uint32_t imageAtlasCursorX = 0;
    uint32_t imageAtlasCursorY = 0;
    uint32_t imageAtlasRowHeight = 0;

#if LUTE_UI_USE_HARFBUZZ_GPU
    hb_gpu_draw_t* draw = nullptr;
    hb_gpu_paint_t* paint = nullptr;
    std::unordered_map<GlyphCacheKey, EncodedGlyph, GlyphCacheKeyHash> glyphCache;
    std::unordered_map<GlyphCacheKey, ImageGlyph, GlyphCacheKeyHash> imageGlyphCache;
#endif
};

DawnBackend& backend();

#endif

} // namespace lute::ui::dawn
