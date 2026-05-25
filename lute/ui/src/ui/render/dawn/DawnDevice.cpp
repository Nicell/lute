#include "lute/ui/Render.h"

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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lute::ui
{

#if LUTE_UI_USE_DAWN
namespace
{

constexpr uint64_t kAtlasCapacity = 256 * 1024;

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
};

struct EncodedGlyph
{
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    uint32_t atlasOffset = 0;
    bool empty = true;
};

static uint64_t alignTo(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static float srgbChannelToLinear(uint8_t value)
{
    float channel = static_cast<float>(value) / 255.0f;
    if (channel <= 0.04045f)
        return channel / 12.92f;
    return std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

static std::array<float, 4> colorToLinearFloat(Color color)
{
    return {
        srgbChannelToLinear(color.r),
        srgbChannelToLinear(color.g),
        srgbChannelToLinear(color.b),
        static_cast<float>(color.a) / 255.0f,
    };
}

static wgpu::Color colorToLinearDawnColor(Color color)
{
    auto rgba = colorToLinearFloat(color);
    return {rgba[0], rgba[1], rgba[2], rgba[3]};
}

static bool isSrgbTextureFormat(wgpu::TextureFormat format)
{
    return format == wgpu::TextureFormat::BGRA8UnormSrgb || format == wgpu::TextureFormat::RGBA8UnormSrgb;
}

static wgpu::TextureFormat chooseSurfaceFormat(const wgpu::SurfaceCapabilities& capabilities)
{
    for (uint32_t i = 0; i < capabilities.formatCount; i++)
    {
        if (capabilities.formats[i] == wgpu::TextureFormat::BGRA8UnormSrgb)
            return capabilities.formats[i];
    }

    for (uint32_t i = 0; i < capabilities.formatCount; i++)
    {
        if (isSrgbTextureFormat(capabilities.formats[i]))
            return capabilities.formats[i];
    }

    return capabilities.formats[0];
}

static wgpu::ShaderModule createShaderModule(const wgpu::Device& device, const std::string& source)
{
    wgpu::ShaderSourceWGSL wgsl;
    wgsl.code = source.c_str();

    wgpu::ShaderModuleDescriptor descriptor;
    descriptor.nextInChain = &wgsl;
    return device.CreateShaderModule(&descriptor);
}

static wgpu::Buffer createBuffer(const wgpu::Device& device, uint64_t size, wgpu::BufferUsage usage)
{
    wgpu::BufferDescriptor descriptor;
    descriptor.size = std::max<uint64_t>(4, alignTo(size, 4));
    descriptor.usage = usage;
    return device.CreateBuffer(&descriptor);
}

static bool isGoodSurfaceTexture(wgpu::SurfaceGetCurrentTextureStatus status)
{
    return status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal || status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal;
}

static wgpu::PresentMode choosePresentMode(const wgpu::SurfaceCapabilities& capabilities)
{
    for (size_t i = 0; i < capabilities.presentModeCount; i++)
    {
        if (capabilities.presentModes[i] == wgpu::PresentMode::Fifo)
            return wgpu::PresentMode::Fifo;
    }

    return capabilities.presentModeCount > 0 ? capabilities.presentModes[0] : wgpu::PresentMode::Fifo;
}

static wgpu::CompositeAlphaMode chooseAlphaMode(const wgpu::SurfaceCapabilities& capabilities)
{
    for (size_t i = 0; i < capabilities.alphaModeCount; i++)
    {
        if (capabilities.alphaModes[i] == wgpu::CompositeAlphaMode::Opaque)
            return wgpu::CompositeAlphaMode::Opaque;
    }

    return capabilities.alphaModeCount > 0 ? capabilities.alphaModes[0] : wgpu::CompositeAlphaMode::Auto;
}

static void appendSolidRect(std::vector<SolidVertex>& vertices, Rect rect, float radius, Color color, float scale)
{
    float x = rect.x * scale;
    float y = rect.y * scale;
    float width = rect.width * scale;
    float height = rect.height * scale;

    if (width <= 0.0f || height <= 0.0f)
        return;

    float clampedRadius = std::clamp(radius * scale, 0.0f, std::min(width, height) * 0.5f);
    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;
    auto rgba = colorToLinearFloat(color);

    auto push = [&](float px, float py, float localX, float localY)
    {
        SolidVertex vertex;
        vertex.position[0] = px;
        vertex.position[1] = py;
        vertex.local[0] = localX;
        vertex.local[1] = localY;
        vertex.rect[0] = halfWidth;
        vertex.rect[1] = halfHeight;
        vertex.rect[2] = clampedRadius;
        vertex.rect[3] = 0.0f;
        std::copy(rgba.begin(), rgba.end(), vertex.color);
        vertices.push_back(vertex);
    };

    push(x, y, 0.0f, 0.0f);
    push(x, y + height, 0.0f, height);
    push(x + width, y, width, 0.0f);

    push(x, y + height, 0.0f, height);
    push(x + width, y, width, 0.0f);
    push(x + width, y + height, width, height);
}

static void appendGlyphVertices(
    std::vector<GlyphVertex>& vertices,
    float x,
    float y,
    float fontSize,
    uint32_t upem,
    const EncodedGlyph& glyph,
    const std::array<float, 4>& color
)
{
    if (glyph.empty || upem == 0 || fontSize <= 0.0f)
        return;

    float scale = fontSize / static_cast<float>(upem);
    float emPerPos = 1.0f / scale;
    GlyphVertex quad[4];

    for (int i = 0; i < 4; i++)
    {
        int cornerX = (i >> 1) & 1;
        int cornerY = i & 1;

        float ex = cornerX ? glyph.maxX : glyph.minX;
        float ey = cornerY ? glyph.maxY : glyph.minY;
        GlyphVertex vertex;
        vertex.position[0] = x + scale * ex;
        vertex.position[1] = y - scale * ey;
        vertex.texcoord[0] = ex;
        vertex.texcoord[1] = ey;
        vertex.normal[0] = cornerX ? 1.0f : -1.0f;
        vertex.normal[1] = cornerY ? -1.0f : 1.0f;
        vertex.emPerPos = emPerPos;
        vertex.atlasOffset = glyph.atlasOffset;
        std::copy(color.begin(), color.end(), vertex.color);
        quad[i] = vertex;
    }

    vertices.push_back(quad[0]);
    vertices.push_back(quad[1]);
    vertices.push_back(quad[2]);

    vertices.push_back(quad[1]);
    vertices.push_back(quad[2]);
    vertices.push_back(quad[3]);
}

class DawnBackend
{
public:
    ~DawnBackend()
    {
#if LUTE_UI_USE_HARFBUZZ_GPU
        hb_gpu_paint_destroy(paint);
        hb_font_destroy(font);
        hb_face_destroy(face);
        hb_blob_destroy(blob);
#endif
    }

    bool renderScene(const Scene& scene)
    {
        if (!ensureDevice(false, nullptr))
            return false;

        constexpr uint32_t width = 800;
        constexpr uint32_t height = 600;
        constexpr wgpu::TextureFormat format = wgpu::TextureFormat::RGBA8UnormSrgb;

        wgpu::TextureDescriptor textureDescriptor;
        textureDescriptor.size = {width, height, 1};
        textureDescriptor.format = format;
        textureDescriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        wgpu::Texture texture = device.CreateTexture(&textureDescriptor);
        if (!texture)
            return false;

        return renderIntoView(texture.CreateView(), format, scene, width, height, 1.0f, false);
    }

    bool renderToMetalLayer(const Scene& scene, void* metalLayer, uint32_t pixelWidth, uint32_t pixelHeight, float scale)
    {
        if (!metalLayer || pixelWidth == 0 || pixelHeight == 0 || scale <= 0.0f)
            return false;

        if (!ensureInstance() || !ensureSurfaceObject(metalLayer) || !ensureDevice(true, &surface) || !configureSurface(pixelWidth, pixelHeight))
            return false;

        wgpu::SurfaceTexture surfaceTexture;
        surface.GetCurrentTexture(&surfaceTexture);
        if (!isGoodSurfaceTexture(surfaceTexture.status))
        {
            surfaceConfigured = false;
            if (!configureSurface(pixelWidth, pixelHeight))
                return false;
            surface.GetCurrentTexture(&surfaceTexture);
        }

        if (!isGoodSurfaceTexture(surfaceTexture.status) || !surfaceTexture.texture)
            return false;

        if (!renderIntoView(surfaceTexture.texture.CreateView(), surfaceFormat, scene, pixelWidth, pixelHeight, scale, true))
            return false;

        return surface.Present();
    }

private:
    bool ensureInstance()
    {
        if (instance)
            return true;

        static constexpr auto timedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
        wgpu::InstanceDescriptor instanceDescriptor;
        instanceDescriptor.requiredFeatureCount = 1;
        instanceDescriptor.requiredFeatures = &timedWaitAny;
        instance = wgpu::CreateInstance(&instanceDescriptor);
        if (!instance)
            return false;

        return true;
    }

    bool ensureDevice(bool requireMetal, const wgpu::Surface* compatibleSurface)
    {
        if (ready && (!requireMetal || metalDevice))
            return true;

        if (ready)
            resetDeviceObjects(false);

        if (!ensureInstance())
            return false;

        AdapterRequest adapterRequest;
        wgpu::RequestAdapterOptions adapterOptions;
#if defined(__APPLE__)
        if (requireMetal)
            adapterOptions.backendType = wgpu::BackendType::Metal;
#endif
        if (compatibleSurface)
            adapterOptions.compatibleSurface = *compatibleSurface;

        wgpu::Future adapterFuture = instance.RequestAdapter(
            &adapterOptions,
            wgpu::CallbackMode::WaitAnyOnly,
            [](wgpu::RequestAdapterStatus status, wgpu::Adapter nextAdapter, wgpu::StringView, AdapterRequest* request)
            {
                request->ok = status == wgpu::RequestAdapterStatus::Success;
                if (request->ok)
                    request->adapter = std::move(nextAdapter);
            },
            &adapterRequest
        );

        if (instance.WaitAny(adapterFuture, UINT64_MAX) != wgpu::WaitStatus::Success || !adapterRequest.ok || !adapterRequest.adapter)
            return false;

        adapter = std::move(adapterRequest.adapter);

        DeviceRequest deviceRequest;
        wgpu::DeviceDescriptor deviceDescriptor;
        deviceDescriptor.SetDeviceLostCallback(
            wgpu::CallbackMode::AllowSpontaneous,
            [](const wgpu::Device&, wgpu::DeviceLostReason, wgpu::StringView)
            {
            }
        );
        deviceDescriptor.SetUncapturedErrorCallback(
            [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message)
            {
                const char* typeName = "Unknown";
                switch (type)
                {
                case wgpu::ErrorType::Validation:
                    typeName = "Validation";
                    break;
                case wgpu::ErrorType::OutOfMemory:
                    typeName = "OutOfMemory";
                    break;
                case wgpu::ErrorType::Internal:
                    typeName = "Internal";
                    break;
                case wgpu::ErrorType::Unknown:
                    typeName = "Unknown";
                    break;
                case wgpu::ErrorType::NoError:
                    typeName = "NoError";
                    break;
                }
                std::cerr << "Dawn " << typeName << " error: " << std::string_view(message) << "\n";
            }
        );
        wgpu::Future deviceFuture = adapter.RequestDevice(
            &deviceDescriptor,
            wgpu::CallbackMode::WaitAnyOnly,
            [](wgpu::RequestDeviceStatus status, wgpu::Device nextDevice, wgpu::StringView, DeviceRequest* request)
            {
                request->ok = status == wgpu::RequestDeviceStatus::Success;
                if (request->ok)
                    request->device = std::move(nextDevice);
            },
            &deviceRequest
        );

        if (instance.WaitAny(deviceFuture, UINT64_MAX) != wgpu::WaitStatus::Success || !deviceRequest.ok || !deviceRequest.device)
            return false;

        device = std::move(deviceRequest.device);
        queue = device.GetQueue();
        ready = true;
        metalDevice = requireMetal;
        return true;
    }

    void resetDeviceObjects(bool clearSurface)
    {
        adapter = {};
        device = {};
        queue = {};
        solidPipeline = {};
        textPipeline = {};
        surfaceBindGroupLayout = {};
        textBindGroupLayout = {};
        surfaceBindGroup = {};
        textBindGroup = {};
        surfaceUniformBuffer = {};
        textUniformBuffer = {};
        solidVertexBuffer = {};
        glyphVertexBuffer = {};
        atlasBuffer = {};
        solidVertexCapacity = 0;
        glyphVertexCapacity = 0;
        if (clearSurface)
        {
            surface = {};
            surfaceLayer = nullptr;
            surfaceFormat = wgpu::TextureFormat::Undefined;
            surfaceWidth = 0;
            surfaceHeight = 0;
        }
        surfaceConfigured = false;
        pipelineFormat = wgpu::TextureFormat::Undefined;
        atlasDirty = atlasCursor > 0;
        ready = false;
        metalDevice = false;
    }

    bool ensureSurfaceObject(void* metalLayer)
    {
        if (!surface || surfaceLayer != metalLayer)
        {
            wgpu::SurfaceSourceMetalLayer metalSource;
            metalSource.layer = metalLayer;

            wgpu::SurfaceDescriptor surfaceDescriptor;
            surfaceDescriptor.nextInChain = &metalSource;
            surface = instance.CreateSurface(&surfaceDescriptor);
            surfaceLayer = metalLayer;
            surfaceConfigured = false;
        }

        return surface != nullptr;
    }

    bool configureSurface(uint32_t pixelWidth, uint32_t pixelHeight)
    {
        if (!surface)
            return false;

        if (surfaceConfigured && surfaceWidth == pixelWidth && surfaceHeight == pixelHeight)
            return true;

        wgpu::SurfaceCapabilities capabilities;
        if (!surface.GetCapabilities(adapter, &capabilities) || capabilities.formatCount == 0)
            return false;

        surfaceFormat = chooseSurfaceFormat(capabilities);

        wgpu::SurfaceConfiguration config;
        config.device = device;
        config.format = surfaceFormat;
        config.usage = wgpu::TextureUsage::RenderAttachment;
        config.width = pixelWidth;
        config.height = pixelHeight;
        config.presentMode = choosePresentMode(capabilities);
        config.alphaMode = chooseAlphaMode(capabilities);
        surface.Configure(&config);

        surfaceWidth = pixelWidth;
        surfaceHeight = pixelHeight;
        surfaceConfigured = true;
        return true;
    }

    bool renderIntoView(const wgpu::TextureView& view, wgpu::TextureFormat format, const Scene& scene, uint32_t width, uint32_t height, float scale, bool presented)
    {
        if (!view || !ensurePipelines(format))
            return false;

        std::vector<SolidVertex> solidVertices;
        std::vector<GlyphVertex> glyphVertices;
        buildSceneVertices(scene, solidVertices, glyphVertices, scale);

        SurfaceUniforms surfaceUniforms;
        surfaceUniforms.viewport[0] = static_cast<float>(width);
        surfaceUniforms.viewport[1] = static_cast<float>(height);
        queue.WriteBuffer(surfaceUniformBuffer, 0, &surfaceUniforms, sizeof(surfaceUniforms));

#if LUTE_UI_USE_HARFBUZZ_GPU
        TextUniforms textUniforms;
        setPixelMvp(textUniforms, width, height);
        textUniforms.viewport[0] = static_cast<float>(width);
        textUniforms.viewport[1] = static_cast<float>(height);
        queue.WriteBuffer(textUniformBuffer, 0, &textUniforms, sizeof(textUniforms));
#endif

        writeVertexData(solidVertexBuffer, solidVertexCapacity, solidVertices);
        writeVertexData(glyphVertexBuffer, glyphVertexCapacity, glyphVertices);

        if (atlasDirty && atlasCursor > 0)
        {
            queue.WriteBuffer(atlasBuffer, 0, atlasShadow.data(), atlasCursor * 4 * sizeof(int32_t));
            atlasDirty = false;
        }

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::RenderPassColorAttachment colorAttachment;
        colorAttachment.view = view;
        colorAttachment.loadOp = wgpu::LoadOp::Clear;
        colorAttachment.storeOp = wgpu::StoreOp::Store;
        colorAttachment.clearValue = colorToLinearDawnColor({245, 245, 242, 255});

        wgpu::RenderPassDescriptor renderPassDescriptor;
        renderPassDescriptor.colorAttachmentCount = 1;
        renderPassDescriptor.colorAttachments = &colorAttachment;

        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPassDescriptor);
        if (!solidVertices.empty())
        {
            pass.SetPipeline(solidPipeline);
            pass.SetBindGroup(0, surfaceBindGroup);
            pass.SetVertexBuffer(0, solidVertexBuffer, 0, solidVertices.size() * sizeof(SolidVertex));
            pass.Draw(static_cast<uint32_t>(solidVertices.size()));
        }

        if (!glyphVertices.empty())
        {
            pass.SetPipeline(textPipeline);
            pass.SetBindGroup(0, textBindGroup);
            pass.SetVertexBuffer(0, glyphVertexBuffer, 0, glyphVertices.size() * sizeof(GlyphVertex));
            pass.Draw(static_cast<uint32_t>(glyphVertices.size()));
        }
        pass.End();

        wgpu::CommandBuffer commands = encoder.Finish();
        queue.Submit(1, &commands);
        (void)presented;
        return true;
    }

    void buildSceneVertices(const Scene& scene, std::vector<SolidVertex>& solidVertices, std::vector<GlyphVertex>& glyphVertices, float scale)
    {
        for (const DisplayItem& item : scene.items())
        {
            switch (item.kind)
            {
            case DisplayItemKind::Rect:
                appendSolidRect(solidVertices, item.rect, 0.0f, item.fill.color, scale);
                break;
            case DisplayItemKind::RoundedRect:
                appendSolidRect(solidVertices, item.rect, item.radius, item.fill.color, scale);
                break;
            case DisplayItemKind::TextRun:
                appendTextRun(glyphVertices, item, scale);
                break;
            case DisplayItemKind::ClipPush:
            case DisplayItemKind::ClipPop:
            case DisplayItemKind::TransformPush:
            case DisplayItemKind::TransformPop:
            case DisplayItemKind::OpacityPush:
            case DisplayItemKind::OpacityPop:
                break;
            }
        }
    }

    void appendTextRun(std::vector<GlyphVertex>& vertices, const DisplayItem& item, float scale)
    {
#if LUTE_UI_USE_HARFBUZZ_GPU
        if (item.text.empty() || !ensureFont())
            return;

        int textLength = static_cast<int>(std::min<size_t>(item.text.size(), static_cast<size_t>(std::numeric_limits<int>::max())));
        hb_buffer_t* buffer = hb_buffer_create();
        hb_buffer_add_utf8(buffer, item.text.data(), textLength, 0, textLength);
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(font, buffer, nullptr, 0);

        unsigned glyphCount = 0;
        hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
        hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyphCount);

        float fontSize = 14.0f * scale;
        float fontScale = upem > 0 ? fontSize / static_cast<float>(upem) : 1.0f;
        float penX = item.origin.x * scale;
        float penY = item.origin.y * scale;
        auto color = colorToLinearFloat(item.fill.color);

        for (unsigned i = 0; i < glyphCount; i++)
        {
            const EncodedGlyph* glyph = lookupGlyph(infos[i].codepoint);
            if (glyph)
            {
                float glyphX = penX + static_cast<float>(positions[i].x_offset) * fontScale;
                float glyphY = penY - static_cast<float>(positions[i].y_offset) * fontScale;
                appendGlyphVertices(vertices, glyphX, glyphY, fontSize, upem, *glyph, color);
            }

            penX += static_cast<float>(positions[i].x_advance) * fontScale;
            penY -= static_cast<float>(positions[i].y_advance) * fontScale;
        }

        hb_buffer_destroy(buffer);
#else
        (void)vertices;
        (void)item;
        (void)scale;
#endif
    }

#if LUTE_UI_USE_HARFBUZZ_GPU
    bool ensureFont()
    {
        if (font && paint)
            return true;

        static constexpr const char* fontPaths[] = {
            "/System/Library/Fonts/SFNS.ttf",
            "/System/Library/Fonts/SFCompact.ttf",
            "/System/Library/Fonts/HelveticaNeue.ttc",
            "/System/Library/Fonts/Geneva.ttf",
        };

        for (const char* path : fontPaths)
        {
            hb_blob_t* nextBlob = hb_blob_create_from_file_or_fail(path);
            if (!nextBlob)
                continue;

            hb_face_t* nextFace = hb_face_create(nextBlob, 0);
            if (!nextFace || hb_face_get_glyph_count(nextFace) == 0)
            {
                hb_face_destroy(nextFace);
                hb_blob_destroy(nextBlob);
                continue;
            }

            blob = nextBlob;
            face = nextFace;
            font = hb_font_create(face);
            hb_ot_font_set_funcs(font);

            upem = hb_face_get_upem(face);
            if (upem == 0)
                upem = 1000;

            hb_font_set_scale(font, static_cast<int>(upem), static_cast<int>(upem));
            paint = hb_gpu_paint_create_or_fail();
            if (!paint)
                return false;

            hb_gpu_paint_set_scale(paint, static_cast<int>(upem), static_cast<int>(upem));
            return true;
        }

        return false;
    }

    const EncodedGlyph* lookupGlyph(hb_codepoint_t glyph)
    {
        auto found = glyphCache.find(glyph);
        if (found != glyphCache.end())
            return &found->second;

        if (!paint || !font)
            return nullptr;

        hb_gpu_paint_clear(paint);
        hb_gpu_paint_glyph(paint, font, glyph);

        hb_glyph_extents_t extents = {};
        hb_blob_t* encoded = hb_gpu_paint_encode(paint, &extents);
        unsigned int encodedLength = encoded ? hb_blob_get_length(encoded) : 0;

        EncodedGlyph result;
        result.minX = static_cast<float>(extents.x_bearing);
        result.maxX = static_cast<float>(extents.x_bearing + extents.width);
        result.maxY = static_cast<float>(extents.y_bearing);
        result.minY = static_cast<float>(extents.y_bearing + extents.height);
        result.empty = encodedLength == 0;

        if (!result.empty)
        {
            const char* encodedData = hb_blob_get_data(encoded, &encodedLength);
            if (!encodedData || !appendAtlas(encodedData, encodedLength, result.atlasOffset))
                result.empty = true;
        }

        if (encoded)
            hb_gpu_paint_recycle_blob(paint, encoded);

        auto [inserted, _] = glyphCache.emplace(glyph, result);
        return &inserted->second;
    }

    bool appendAtlas(const char* data, unsigned int length, uint32_t& offset)
    {
        if (length == 0 || length % 8 != 0)
            return false;

        uint64_t texels = length / 8;
        if (atlasCursor + texels > kAtlasCapacity)
            return false;

        offset = static_cast<uint32_t>(atlasCursor);
        const int16_t* source = reinterpret_cast<const int16_t*>(data);
        int32_t* target = atlasShadow.data() + atlasCursor * 4;
        for (uint64_t i = 0; i < texels * 4; i++)
            target[i] = static_cast<int32_t>(source[i]);

        atlasCursor += texels;
        atlasDirty = true;
        return true;
    }
#endif

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

        queue.WriteBuffer(buffer, 0, vertices.data(), byteSize);
    }

    void setPixelMvp(TextUniforms& uniforms, uint32_t width, uint32_t height)
    {
        std::fill(std::begin(uniforms.mvp), std::end(uniforms.mvp), 0.0f);
        uniforms.mvp[0] = 2.0f / static_cast<float>(width);
        uniforms.mvp[5] = -2.0f / static_cast<float>(height);
        uniforms.mvp[10] = 1.0f;
        uniforms.mvp[12] = -1.0f;
        uniforms.mvp[13] = 1.0f;
        uniforms.mvp[15] = 1.0f;
    }

    bool ensurePipelines(wgpu::TextureFormat format)
    {
        if (pipelineFormat != format)
        {
            pipelineFormat = format;
            solidPipeline = {};
            textPipeline = {};
            surfaceBindGroup = {};
            textBindGroup = {};
        }

        return ensureSolidPipeline(format) && ensureTextPipeline(format);
    }

    bool ensureSolidPipeline(wgpu::TextureFormat format)
    {
        if (solidPipeline && surfaceBindGroup)
            return true;

        if (!surfaceUniformBuffer)
            surfaceUniformBuffer = createBuffer(device, sizeof(SurfaceUniforms), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);

        wgpu::BindGroupLayoutEntry uniformLayout;
        uniformLayout.binding = 0;
        uniformLayout.visibility = wgpu::ShaderStage::Vertex;
        uniformLayout.buffer.type = wgpu::BufferBindingType::Uniform;
        uniformLayout.buffer.minBindingSize = sizeof(SurfaceUniforms);

        wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor;
        bindGroupLayoutDescriptor.entryCount = 1;
        bindGroupLayoutDescriptor.entries = &uniformLayout;
        surfaceBindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

        wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor;
        pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
        pipelineLayoutDescriptor.bindGroupLayouts = &surfaceBindGroupLayout;
        wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDescriptor);

        wgpu::BindGroupEntry uniformEntry;
        uniformEntry.binding = 0;
        uniformEntry.buffer = surfaceUniformBuffer;
        uniformEntry.size = sizeof(SurfaceUniforms);

        wgpu::BindGroupDescriptor bindGroupDescriptor;
        bindGroupDescriptor.layout = surfaceBindGroupLayout;
        bindGroupDescriptor.entryCount = 1;
        bindGroupDescriptor.entries = &uniformEntry;
        surfaceBindGroup = device.CreateBindGroup(&bindGroupDescriptor);

        std::string shaderSource = R"(
struct Uniforms {
  viewport: vec2f,
};

@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexIn {
  @location(0) position: vec2f,
  @location(1) local: vec2f,
  @location(2) rect: vec4f,
  @location(3) color: vec4f,
};

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) local: vec2f,
  @location(1) rect: vec4f,
  @location(2) color: vec4f,
};

fn clip_from_pixel(position: vec2f) -> vec4f {
  let x = position.x / u.viewport.x * 2.0 - 1.0;
  let y = 1.0 - position.y / u.viewport.y * 2.0;
  return vec4f(x, y, 0.0, 1.0);
}

@vertex fn vs(in: VertexIn) -> VertexOut {
  var out: VertexOut;
  out.position = clip_from_pixel(in.position);
  out.local = in.local;
  out.rect = in.rect;
  out.color = in.color;
  return out;
}

@fragment fn fs(in: VertexOut) -> @location(0) vec4f {
  let radius = in.rect.z;
  if (radius <= 0.0) {
    return in.color;
  }

  let half_size = in.rect.xy;
  let p = in.local - half_size;
  let q = abs(p) - (half_size - vec2f(radius));
  let distance = length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - radius;
  let coverage = 1.0 - smoothstep(-1.0, 1.0, distance);
  return vec4f(in.color.rgb, in.color.a * coverage);
}
)";

        wgpu::ShaderModule shader = createShaderModule(device, shaderSource);

        wgpu::VertexAttribute attributes[4];
        attributes[0].format = wgpu::VertexFormat::Float32x2;
        attributes[0].offset = offsetof(SolidVertex, position);
        attributes[0].shaderLocation = 0;
        attributes[1].format = wgpu::VertexFormat::Float32x2;
        attributes[1].offset = offsetof(SolidVertex, local);
        attributes[1].shaderLocation = 1;
        attributes[2].format = wgpu::VertexFormat::Float32x4;
        attributes[2].offset = offsetof(SolidVertex, rect);
        attributes[2].shaderLocation = 2;
        attributes[3].format = wgpu::VertexFormat::Float32x4;
        attributes[3].offset = offsetof(SolidVertex, color);
        attributes[3].shaderLocation = 3;

        wgpu::VertexBufferLayout vertexBufferLayout;
        vertexBufferLayout.arrayStride = sizeof(SolidVertex);
        vertexBufferLayout.attributeCount = 4;
        vertexBufferLayout.attributes = attributes;

        wgpu::BlendState blend;
        blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
        blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        blend.color.operation = wgpu::BlendOperation::Add;
        blend.alpha.srcFactor = wgpu::BlendFactor::One;
        blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        blend.alpha.operation = wgpu::BlendOperation::Add;

        wgpu::ColorTargetState colorTarget;
        colorTarget.format = format;
        colorTarget.blend = &blend;
        colorTarget.writeMask = wgpu::ColorWriteMask::All;

        wgpu::FragmentState fragment;
        fragment.module = shader;
        fragment.entryPoint = "fs";
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;

        wgpu::RenderPipelineDescriptor pipelineDescriptor;
        pipelineDescriptor.layout = pipelineLayout;
        pipelineDescriptor.vertex.module = shader;
        pipelineDescriptor.vertex.entryPoint = "vs";
        pipelineDescriptor.vertex.bufferCount = 1;
        pipelineDescriptor.vertex.buffers = &vertexBufferLayout;
        pipelineDescriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        pipelineDescriptor.primitive.cullMode = wgpu::CullMode::None;
        pipelineDescriptor.fragment = &fragment;

        solidPipeline = device.CreateRenderPipeline(&pipelineDescriptor);
        return solidPipeline && surfaceBindGroup;
    }

    bool ensureTextPipeline(wgpu::TextureFormat format)
    {
#if !LUTE_UI_USE_HARFBUZZ_GPU
        (void)format;
        return true;
#else
        if (textPipeline && textBindGroup)
            return true;

        if (!textUniformBuffer)
            textUniformBuffer = createBuffer(device, sizeof(TextUniforms), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);

        if (!atlasBuffer)
        {
            atlasShadow.assign(kAtlasCapacity * 4, 0);
            atlasBuffer = createBuffer(device, kAtlasCapacity * 4 * sizeof(int32_t), wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
            atlasDirty = true;
        }

        wgpu::BindGroupLayoutEntry entries[2];
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(TextUniforms);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[1].buffer.minBindingSize = kAtlasCapacity * 4 * sizeof(int32_t);

        wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor;
        bindGroupLayoutDescriptor.entryCount = 2;
        bindGroupLayoutDescriptor.entries = entries;
        textBindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

        wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor;
        pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
        pipelineLayoutDescriptor.bindGroupLayouts = &textBindGroupLayout;
        wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDescriptor);

        wgpu::BindGroupEntry bindGroupEntries[2];
        bindGroupEntries[0].binding = 0;
        bindGroupEntries[0].buffer = textUniformBuffer;
        bindGroupEntries[0].size = sizeof(TextUniforms);
        bindGroupEntries[1].binding = 1;
        bindGroupEntries[1].buffer = atlasBuffer;
        bindGroupEntries[1].size = kAtlasCapacity * 4 * sizeof(int32_t);

        wgpu::BindGroupDescriptor bindGroupDescriptor;
        bindGroupDescriptor.layout = textBindGroupLayout;
        bindGroupDescriptor.entryCount = 2;
        bindGroupDescriptor.entries = bindGroupEntries;
        textBindGroup = device.CreateBindGroup(&bindGroupDescriptor);

        std::string shaderSource;
        shaderSource += hb_gpu_shader_source(HB_GPU_SHADER_STAGE_VERTEX, HB_GPU_SHADER_LANG_WGSL);
        shaderSource += "\n";
        shaderSource += hb_gpu_draw_shader_source(HB_GPU_SHADER_STAGE_VERTEX, HB_GPU_SHADER_LANG_WGSL);
        shaderSource += "\n";
        shaderSource += hb_gpu_shader_source(HB_GPU_SHADER_STAGE_FRAGMENT, HB_GPU_SHADER_LANG_WGSL);
        shaderSource += "\n";
        shaderSource += hb_gpu_draw_shader_source(HB_GPU_SHADER_STAGE_FRAGMENT, HB_GPU_SHADER_LANG_WGSL);
        shaderSource += "\n";
        shaderSource += hb_gpu_paint_shader_source(HB_GPU_SHADER_STAGE_FRAGMENT, HB_GPU_SHADER_LANG_WGSL);
        shaderSource += "\n";
        shaderSource += R"(
struct Uniforms {
  mvp: mat4x4f,
  viewport: vec2f,
  stem_darkening: f32,
  debug: f32,
};

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var<storage, read> hb_gpu_atlas: array<vec4<i32>>;

struct VertexInput {
  @location(0) position: vec2f,
  @location(1) texcoord: vec2f,
  @location(2) normal: vec2f,
  @location(3) emPerPos: f32,
  @location(4) glyphLoc: u32,
  @location(5) color: vec4f,
};

struct VertexOutput {
  @builtin(position) clip_position: vec4f,
  @location(0) texcoord: vec2f,
  @location(1) @interpolate(flat) glyphLoc: u32,
  @location(2) color: vec4f,
};

@vertex fn vs_main(in: VertexInput) -> VertexOutput {
  var pos = in.position;
  var tc = in.texcoord;
  let jac = vec4f(in.emPerPos, 0.0, 0.0, -in.emPerPos);
  let result = hb_gpu_dilate(pos, tc, in.normal, jac, u.mvp, u.viewport);
  pos = result[0];
  tc = result[1];

  var out: VertexOutput;
  out.clip_position = u.mvp * vec4f(pos, 0.0, 1.0);
  out.texcoord = tc;
  out.glyphLoc = in.glyphLoc;
  out.color = in.color;
  return out;
}

@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
  let ppem = hb_gpu_ppem(in.texcoord, in.glyphLoc, &hb_gpu_atlas);
  var cov: f32;
  var c = hb_gpu_paint(in.texcoord, in.glyphLoc, in.color, &hb_gpu_atlas, &cov);

  if (cov > 0.0 && cov < 1.0) {
    var adjusted = cov;
    if (u.stem_darkening > 0.0) {
      let brightness = select(0.0, dot(c.rgb, vec3f(1.0 / 3.0)) / c.a, c.a > 0.0);
      adjusted = hb_gpu_stem_darken(adjusted, brightness, ppem);
    }
    c = c * (adjusted / cov);
  }

  return c;
}
)";

        wgpu::ShaderModule shader = createShaderModule(device, shaderSource);

        wgpu::VertexAttribute attributes[6];
        attributes[0].format = wgpu::VertexFormat::Float32x2;
        attributes[0].offset = offsetof(GlyphVertex, position);
        attributes[0].shaderLocation = 0;
        attributes[1].format = wgpu::VertexFormat::Float32x2;
        attributes[1].offset = offsetof(GlyphVertex, texcoord);
        attributes[1].shaderLocation = 1;
        attributes[2].format = wgpu::VertexFormat::Float32x2;
        attributes[2].offset = offsetof(GlyphVertex, normal);
        attributes[2].shaderLocation = 2;
        attributes[3].format = wgpu::VertexFormat::Float32;
        attributes[3].offset = offsetof(GlyphVertex, emPerPos);
        attributes[3].shaderLocation = 3;
        attributes[4].format = wgpu::VertexFormat::Uint32;
        attributes[4].offset = offsetof(GlyphVertex, atlasOffset);
        attributes[4].shaderLocation = 4;
        attributes[5].format = wgpu::VertexFormat::Float32x4;
        attributes[5].offset = offsetof(GlyphVertex, color);
        attributes[5].shaderLocation = 5;

        wgpu::VertexBufferLayout vertexBufferLayout;
        vertexBufferLayout.arrayStride = sizeof(GlyphVertex);
        vertexBufferLayout.attributeCount = 6;
        vertexBufferLayout.attributes = attributes;

        wgpu::BlendState blend;
        blend.color.srcFactor = wgpu::BlendFactor::One;
        blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        blend.color.operation = wgpu::BlendOperation::Add;
        blend.alpha.srcFactor = wgpu::BlendFactor::One;
        blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        blend.alpha.operation = wgpu::BlendOperation::Add;

        wgpu::ColorTargetState colorTarget;
        colorTarget.format = format;
        colorTarget.blend = &blend;
        colorTarget.writeMask = wgpu::ColorWriteMask::All;

        wgpu::FragmentState fragment;
        fragment.module = shader;
        fragment.entryPoint = "fs_main";
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;

        wgpu::RenderPipelineDescriptor pipelineDescriptor;
        pipelineDescriptor.layout = pipelineLayout;
        pipelineDescriptor.vertex.module = shader;
        pipelineDescriptor.vertex.entryPoint = "vs_main";
        pipelineDescriptor.vertex.bufferCount = 1;
        pipelineDescriptor.vertex.buffers = &vertexBufferLayout;
        pipelineDescriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        pipelineDescriptor.primitive.cullMode = wgpu::CullMode::None;
        pipelineDescriptor.fragment = &fragment;

        textPipeline = device.CreateRenderPipeline(&pipelineDescriptor);
        return textPipeline && textBindGroup;
#endif
    }

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
    wgpu::BindGroupLayout surfaceBindGroupLayout;
    wgpu::BindGroupLayout textBindGroupLayout;
    wgpu::BindGroup surfaceBindGroup;
    wgpu::BindGroup textBindGroup;
    wgpu::Buffer surfaceUniformBuffer;
    wgpu::Buffer textUniformBuffer;
    wgpu::Buffer solidVertexBuffer;
    wgpu::Buffer glyphVertexBuffer;
    wgpu::Buffer atlasBuffer;
    uint64_t solidVertexCapacity = 0;
    uint64_t glyphVertexCapacity = 0;

    std::vector<int32_t> atlasShadow;
    uint64_t atlasCursor = 0;
    bool atlasDirty = false;

#if LUTE_UI_USE_HARFBUZZ_GPU
    hb_blob_t* blob = nullptr;
    hb_face_t* face = nullptr;
    hb_font_t* font = nullptr;
    hb_gpu_paint_t* paint = nullptr;
    uint32_t upem = 1000;
    std::unordered_map<hb_codepoint_t, EncodedGlyph> glyphCache;
#endif
};

DawnBackend& backend()
{
    static DawnBackend instance;
    return instance;
}

} // namespace
#endif

RenderStats DawnRenderer::render(const Scene& scene)
{
#if LUTE_UI_USE_DAWN
    if (backend().renderScene(scene))
        return {"Dawn", scene.items().size(), scene.generation()};
#endif

    return {"DawnUnavailable", scene.items().size(), scene.generation()};
}

RenderStats DawnRenderer::renderToMetalLayer(const Scene& scene, void* metalLayer, uint32_t pixelWidth, uint32_t pixelHeight, float scale)
{
#if LUTE_UI_USE_DAWN
    if (backend().renderToMetalLayer(scene, metalLayer, pixelWidth, pixelHeight, scale))
        return {"Dawn", scene.items().size(), scene.generation()};
#else
    (void)metalLayer;
    (void)pixelWidth;
    (void)pixelHeight;
    (void)scale;
#endif

    return {"DawnUnavailable", scene.items().size(), scene.generation()};
}

} // namespace lute::ui
