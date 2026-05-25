#include "DawnInternal.h"

#if defined(__APPLE__) && LUTE_UI_USE_HARFBUZZ_GPU
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

#if LUTE_UI_USE_DAWN
#include <algorithm>
#include <cmath>
#include <cstring>

namespace lute::ui::dawn
{

uint64_t alignTo(uint64_t value, uint64_t alignment)
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

std::array<float, 4> colorToLinearFloat(Color color)
{
    return {
        srgbChannelToLinear(color.r),
        srgbChannelToLinear(color.g),
        srgbChannelToLinear(color.b),
        static_cast<float>(color.a) / 255.0f,
    };
}

wgpu::Color colorToLinearDawnColor(Color color)
{
    auto rgba = colorToLinearFloat(color);
    return {rgba[0], rgba[1], rgba[2], rgba[3]};
}

static bool isSrgbTextureFormat(wgpu::TextureFormat format)
{
    return format == wgpu::TextureFormat::BGRA8UnormSrgb || format == wgpu::TextureFormat::RGBA8UnormSrgb;
}

wgpu::TextureFormat chooseSurfaceFormat(const wgpu::SurfaceCapabilities& capabilities)
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

wgpu::ShaderModule createShaderModule(const wgpu::Device& device, const std::string& source)
{
    wgpu::ShaderSourceWGSL wgsl;
    wgsl.code = source.c_str();

    wgpu::ShaderModuleDescriptor descriptor;
    descriptor.nextInChain = &wgsl;
    return device.CreateShaderModule(&descriptor);
}

wgpu::Buffer createBuffer(const wgpu::Device& device, uint64_t size, wgpu::BufferUsage usage)
{
    wgpu::BufferDescriptor descriptor;
    descriptor.size = std::max<uint64_t>(4, alignTo(size, 4));
    descriptor.usage = usage;
    return device.CreateBuffer(&descriptor);
}

bool isGoodSurfaceTexture(wgpu::SurfaceGetCurrentTextureStatus status)
{
    return status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal || status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal;
}

wgpu::PresentMode choosePresentMode(const wgpu::SurfaceCapabilities& capabilities)
{
    for (size_t i = 0; i < capabilities.presentModeCount; i++)
    {
        if (capabilities.presentModes[i] == wgpu::PresentMode::Fifo)
            return wgpu::PresentMode::Fifo;
    }

    return capabilities.presentModeCount > 0 ? capabilities.presentModes[0] : wgpu::PresentMode::Fifo;
}

wgpu::CompositeAlphaMode chooseAlphaMode(const wgpu::SurfaceCapabilities& capabilities)
{
    for (size_t i = 0; i < capabilities.alphaModeCount; i++)
    {
        if (capabilities.alphaModes[i] == wgpu::CompositeAlphaMode::Opaque)
            return wgpu::CompositeAlphaMode::Opaque;
    }

    return capabilities.alphaModeCount > 0 ? capabilities.alphaModes[0] : wgpu::CompositeAlphaMode::Auto;
}

void appendSolidRect(std::vector<SolidVertex>& vertices, Rect rect, float radius, Color color, float scale)
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

void appendGlyphVertices(
    std::vector<GlyphVertex>& vertices,
    float x,
    float y,
    float fontSize,
    uint32_t upem,
    const EncodedGlyph& glyph,
    const std::array<float, 4>& color,
    const std::array<float, 4>& background
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
        std::copy(background.begin(), background.end(), vertex.background);
        quad[i] = vertex;
    }

    vertices.push_back(quad[0]);
    vertices.push_back(quad[1]);
    vertices.push_back(quad[2]);

    vertices.push_back(quad[1]);
    vertices.push_back(quad[2]);
    vertices.push_back(quad[3]);
}

void appendImageGlyphVertices(std::vector<ImageGlyphVertex>& vertices, float x, float y, float fontSize, uint32_t upem, const ImageGlyph& glyph)
{
    if (glyph.empty || upem == 0 || fontSize <= 0.0f)
        return;

    float scale = fontSize / static_cast<float>(upem);
    ImageGlyphVertex quad[4];

    for (int i = 0; i < 4; i++)
    {
        int cornerX = (i >> 1) & 1;
        int cornerY = i & 1;

        float ex = cornerX ? glyph.maxX : glyph.minX;
        float ey = cornerY ? glyph.maxY : glyph.minY;

        ImageGlyphVertex vertex;
        vertex.position[0] = x + scale * ex;
        vertex.position[1] = y - scale * ey;
        vertex.texcoord[0] = cornerX ? glyph.u1 : glyph.u0;
        vertex.texcoord[1] = cornerY ? glyph.v0 : glyph.v1;
        quad[i] = vertex;
    }

    vertices.push_back(quad[0]);
    vertices.push_back(quad[1]);
    vertices.push_back(quad[2]);

    vertices.push_back(quad[1]);
    vertices.push_back(quad[2]);
    vertices.push_back(quad[3]);
}

#if LUTE_UI_USE_HARFBUZZ_GPU && defined(__APPLE__)
std::optional<DecodedImage> decodeImageBlob(hb_blob_t* blob)
{
    if (!blob)
        return std::nullopt;

    unsigned int length = 0;
    const char* data = hb_blob_get_data(blob, &length);
    if (!data || length == 0)
        return std::nullopt;

    CFDataRef cfData = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(data), static_cast<CFIndex>(length));
    if (!cfData)
        return std::nullopt;

    CGImageSourceRef source = CGImageSourceCreateWithData(cfData, nullptr);
    CFRelease(cfData);
    if (!source)
        return std::nullopt;

    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!image)
        return std::nullopt;

    size_t width = CGImageGetWidth(image);
    size_t height = CGImageGetHeight(image);
    if (width == 0 || height == 0 || width > kImageAtlasSize || height > kImageAtlasSize)
    {
        CGImageRelease(image);
        return std::nullopt;
    }

    DecodedImage decoded;
    decoded.width = static_cast<uint32_t>(width);
    decoded.height = static_cast<uint32_t>(height);
    decoded.pixels.assign(width * height * 4, 0);

    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(
        decoded.pixels.data(),
        width,
        height,
        8,
        width * 4,
        colorSpace,
        static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Big) | static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast)
    );

    if (colorSpace)
        CGColorSpaceRelease(colorSpace);

    if (!context)
    {
        CGImageRelease(image);
        return std::nullopt;
    }

    CGContextDrawImage(context, CGRectMake(0.0, 0.0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)), image);
    CGContextRelease(context);
    CGImageRelease(image);
    return decoded;
}
#elif LUTE_UI_USE_HARFBUZZ_GPU
std::optional<DecodedImage> decodeImageBlob(hb_blob_t*)
{
    return std::nullopt;
}
#endif

} // namespace lute::ui::dawn
#endif
