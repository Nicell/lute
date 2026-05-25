#include "DawnInternal.h"

#if LUTE_UI_USE_DAWN
#include <algorithm>
#include <climits>
#include <iostream>
#include <limits>
#include <memory>

namespace lute::ui::dawn
{

DawnBackend::~DawnBackend()
{
#if LUTE_UI_USE_HARFBUZZ_GPU
    hb_gpu_draw_destroy(draw);
    hb_gpu_paint_destroy(paint);
#endif
}

bool DawnBackend::renderScene(const Scene& scene)
{
    {
        ProfileZone zone(ProfilePhase::PipelineSetup);
        if (!ensureDevice(false, nullptr))
            return false;
    }

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

    return renderIntoView(texture.CreateView(), format, scene, width, height, 1.0f);
}

bool DawnBackend::renderToMetalLayer(const Scene& scene, void* metalLayer, uint32_t pixelWidth, uint32_t pixelHeight, float scale)
{
    if (!metalLayer || pixelWidth == 0 || pixelHeight == 0 || scale <= 0.0f)
        return false;

    {
        ProfileZone zone(ProfilePhase::PipelineSetup);
        if (!ensureInstance() || !ensureSurfaceObject(metalLayer) || !ensureDevice(true, &surface) || !configureSurface(pixelWidth, pixelHeight))
            return false;
    }

    wgpu::SurfaceTexture surfaceTexture;
    {
        ProfileZone zone(ProfilePhase::SurfaceAcquire);
        surface.GetCurrentTexture(&surfaceTexture);
    }
    if (!isGoodSurfaceTexture(surfaceTexture.status))
    {
        surfaceConfigured = false;
        {
            ProfileZone zone(ProfilePhase::PipelineSetup);
            if (!configureSurface(pixelWidth, pixelHeight))
                return false;
        }
        {
            ProfileZone zone(ProfilePhase::SurfaceAcquire);
            surface.GetCurrentTexture(&surfaceTexture);
        }
    }

    if (!isGoodSurfaceTexture(surfaceTexture.status) || !surfaceTexture.texture)
        return false;

    if (!renderIntoView(surfaceTexture.texture.CreateView(), surfaceFormat, scene, pixelWidth, pixelHeight, scale))
        return false;

    {
        ProfileZone zone(ProfilePhase::Present);
        return surface.Present();
    }
}

bool DawnBackend::ensureInstance()
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

bool DawnBackend::ensureDevice(bool requireMetal, const wgpu::Surface* compatibleSurface)
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

void DawnBackend::resetDeviceObjects(bool clearSurface)
{
    adapter = {};
    device = {};
    queue = {};
    solidPipeline = {};
    textPipeline = {};
    paintTextPipeline = {};
    imageGlyphPipeline = {};
    surfaceBindGroupLayout = {};
    textBindGroupLayout = {};
    imageGlyphBindGroupLayout = {};
    surfaceBindGroup = {};
    textBindGroup = {};
    imageGlyphBindGroup = {};
    surfaceUniformBuffer = {};
    textUniformBuffer = {};
    solidVertexBuffer = {};
    glyphVertexBuffer = {};
    paintGlyphVertexBuffer = {};
    imageGlyphVertexBuffer = {};
    atlasBuffer = {};
    imageAtlasTexture = {};
    imageAtlasTextureView = {};
    imageAtlasSampler = {};
    solidVertexCapacity = 0;
    glyphVertexCapacity = 0;
    paintGlyphVertexCapacity = 0;
    imageGlyphVertexCapacity = 0;
#if LUTE_UI_USE_HARFBUZZ_GPU
    imageGlyphCache.clear();
#endif
    imageAtlasCursorX = 0;
    imageAtlasCursorY = 0;
    imageAtlasRowHeight = 0;
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
    atlasDirtyStart = atlasCursor > 0 ? 0 : kAtlasCapacity;
    atlasDirtyEnd = atlasCursor;
    ready = false;
    metalDevice = false;
}

bool DawnBackend::ensureSurfaceObject(void* metalLayer)
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

bool DawnBackend::configureSurface(uint32_t pixelWidth, uint32_t pixelHeight)
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

bool DawnBackend::renderIntoView(const wgpu::TextureView& view, wgpu::TextureFormat format, const Scene& scene, uint32_t width, uint32_t height, float scale)
{
    if (!view)
        return false;

    {
        ProfileZone zone(ProfilePhase::PipelineSetup);
        if (!ensurePipelines(format))
            return false;
    }

    FrameVertices vertices;
    {
        ProfileZone zone(ProfilePhase::RenderPrepare);
        buildSceneVertices(scene, vertices, scale);
    }

    SurfaceUniforms surfaceUniforms;
    surfaceUniforms.viewport[0] = static_cast<float>(width);
    surfaceUniforms.viewport[1] = static_cast<float>(height);
    {
        ProfileZone zone(ProfilePhase::Upload);
        UiProfiler::addBufferUpload(sizeof(surfaceUniforms));
        queue.WriteBuffer(surfaceUniformBuffer, 0, &surfaceUniforms, sizeof(surfaceUniforms));
    }

#if LUTE_UI_USE_HARFBUZZ_GPU
    TextUniforms textUniforms;
    setPixelMvp(textUniforms, width, height);
    textUniforms.viewport[0] = static_cast<float>(width);
    textUniforms.viewport[1] = static_cast<float>(height);
    {
        ProfileZone zone(ProfilePhase::Upload);
        UiProfiler::addBufferUpload(sizeof(textUniforms));
        queue.WriteBuffer(textUniformBuffer, 0, &textUniforms, sizeof(textUniforms));
    }
#endif

    writeVertexData(solidVertexBuffer, solidVertexCapacity, vertices.solid);
    writeVertexData(glyphVertexBuffer, glyphVertexCapacity, vertices.drawGlyphs);
    writeVertexData(paintGlyphVertexBuffer, paintGlyphVertexCapacity, vertices.paintGlyphs);
    writeVertexData(imageGlyphVertexBuffer, imageGlyphVertexCapacity, vertices.imageGlyphs);

#if LUTE_UI_USE_HARFBUZZ_GPU
    if (atlasDirtyEnd > atlasDirtyStart)
    {
        uint64_t dirtyTexels = atlasDirtyEnd - atlasDirtyStart;
        uint64_t dirtyBytes = dirtyTexels * 4 * sizeof(int32_t);
        uint64_t dirtyOffset = atlasDirtyStart * 4 * sizeof(int32_t);
        ProfileZone zone(ProfilePhase::Upload);
        UiProfiler::addBufferUpload(dirtyBytes);
        queue.WriteBuffer(atlasBuffer, dirtyOffset, atlasShadow.data() + atlasDirtyStart * 4, dirtyBytes);
        clearAtlasDirtyRange();
    }
#endif

    wgpu::CommandBuffer commands;
    {
        ProfileZone encodeZone(ProfilePhase::RenderEncode);
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
        if (!vertices.solid.empty())
        {
            UiProfiler::addDrawCall();
            pass.SetPipeline(solidPipeline);
            pass.SetBindGroup(0, surfaceBindGroup);
            pass.SetVertexBuffer(0, solidVertexBuffer, 0, vertices.solid.size() * sizeof(SolidVertex));
            pass.Draw(static_cast<uint32_t>(vertices.solid.size()));
        }

        if (!vertices.drawGlyphs.empty())
        {
            UiProfiler::addDrawCall();
            pass.SetPipeline(textPipeline);
            pass.SetBindGroup(0, textBindGroup);
            pass.SetVertexBuffer(0, glyphVertexBuffer, 0, vertices.drawGlyphs.size() * sizeof(GlyphVertex));
            pass.Draw(static_cast<uint32_t>(vertices.drawGlyphs.size()));
        }

        if (!vertices.paintGlyphs.empty())
        {
            UiProfiler::addDrawCall();
            pass.SetPipeline(paintTextPipeline);
            pass.SetBindGroup(0, textBindGroup);
            pass.SetVertexBuffer(0, paintGlyphVertexBuffer, 0, vertices.paintGlyphs.size() * sizeof(GlyphVertex));
            pass.Draw(static_cast<uint32_t>(vertices.paintGlyphs.size()));
        }

        if (!vertices.imageGlyphs.empty())
        {
            UiProfiler::addDrawCall();
            pass.SetPipeline(imageGlyphPipeline);
            pass.SetBindGroup(0, surfaceBindGroup);
            pass.SetBindGroup(1, imageGlyphBindGroup);
            pass.SetVertexBuffer(0, imageGlyphVertexBuffer, 0, vertices.imageGlyphs.size() * sizeof(ImageGlyphVertex));
            pass.Draw(static_cast<uint32_t>(vertices.imageGlyphs.size()));
        }
        pass.End();

        commands = encoder.Finish();
    }
    {
        ProfileZone submitZone(ProfilePhase::Submit);
        queue.Submit(1, &commands);
    }
    return true;
}

void DawnBackend::buildSceneVertices(const Scene& scene, FrameVertices& vertices, float scale)
{
    for (const DisplayItem& item : scene.items())
    {
        switch (item.kind)
        {
        case DisplayItemKind::Rect:
            appendSolidRect(vertices.solid, item.rect, 0.0f, item.fill.color, scale);
            break;
        case DisplayItemKind::RoundedRect:
            appendSolidRect(vertices.solid, item.rect, item.radius, item.fill.color, scale);
            break;
        case DisplayItemKind::TextRun:
            appendTextRun(vertices, item, scale);
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

void DawnBackend::setPixelMvp(TextUniforms& uniforms, uint32_t width, uint32_t height)
{
    std::fill(std::begin(uniforms.mvp), std::end(uniforms.mvp), 0.0f);
    uniforms.mvp[0] = 2.0f / static_cast<float>(width);
    uniforms.mvp[5] = -2.0f / static_cast<float>(height);
    uniforms.mvp[10] = 1.0f;
    uniforms.mvp[12] = -1.0f;
    uniforms.mvp[13] = 1.0f;
    uniforms.mvp[15] = 1.0f;
}

DawnBackend& backend()
{
    static DawnBackend instance;
    return instance;
}

} // namespace lute::ui::dawn
#endif
