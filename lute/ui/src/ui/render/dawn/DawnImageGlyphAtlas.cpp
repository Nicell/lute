#include "DawnInternal.h"

#if LUTE_UI_USE_DAWN
#include <algorithm>
#include <cmath>
#include <vector>

namespace lute::ui::dawn
{

#if LUTE_UI_USE_HARFBUZZ_GPU
namespace
{

uint32_t alignImageAtlasCoord(uint32_t value)
{
    return (value + kImageAtlasAlignment - 1) & ~(kImageAtlasAlignment - 1);
}

std::vector<uint8_t> makeGutteredImage(const DecodedImage& image)
{
    uint32_t width = image.width + 2 * kImageAtlasGutter;
    uint32_t height = image.height + 2 * kImageAtlasGutter;
    std::vector<uint8_t> pixels(width * height * 4, 0);

    for (uint32_t y = 0; y < height; y++)
    {
        uint32_t sourceY = static_cast<uint32_t>(
            std::clamp<int>(static_cast<int>(y) - static_cast<int>(kImageAtlasGutter), 0, static_cast<int>(image.height) - 1)
        );
        for (uint32_t x = 0; x < width; x++)
        {
            uint32_t sourceX = static_cast<uint32_t>(
                std::clamp<int>(static_cast<int>(x) - static_cast<int>(kImageAtlasGutter), 0, static_cast<int>(image.width) - 1)
            );
            size_t source = (static_cast<size_t>(sourceY) * image.width + sourceX) * 4;
            size_t target = (static_cast<size_t>(y) * width + x) * 4;
            pixels[target + 0] = image.pixels[source + 0];
            pixels[target + 1] = image.pixels[source + 1];
            pixels[target + 2] = image.pixels[source + 2];
            pixels[target + 3] = image.pixels[source + 3];
        }
    }

    return pixels;
}

std::vector<uint8_t> downsampleRgba(const std::vector<uint8_t>& source, uint32_t sourceWidth, uint32_t sourceHeight)
{
    uint32_t width = std::max(1u, (sourceWidth + 1) / 2);
    uint32_t height = std::max(1u, (sourceHeight + 1) / 2);
    std::vector<uint8_t> pixels(width * height * 4, 0);

    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            uint32_t channels[4] = {};
            uint32_t count = 0;
            for (uint32_t dy = 0; dy < 2; dy++)
            {
                uint32_t sourceY = y * 2 + dy;
                if (sourceY >= sourceHeight)
                    continue;

                for (uint32_t dx = 0; dx < 2; dx++)
                {
                    uint32_t sourceX = x * 2 + dx;
                    if (sourceX >= sourceWidth)
                        continue;

                    size_t sourceOffset = (static_cast<size_t>(sourceY) * sourceWidth + sourceX) * 4;
                    channels[0] += source[sourceOffset + 0];
                    channels[1] += source[sourceOffset + 1];
                    channels[2] += source[sourceOffset + 2];
                    channels[3] += source[sourceOffset + 3];
                    count++;
                }
            }

            size_t target = (static_cast<size_t>(y) * width + x) * 4;
            pixels[target + 0] = static_cast<uint8_t>((channels[0] + count / 2) / count);
            pixels[target + 1] = static_cast<uint8_t>((channels[1] + count / 2) / count);
            pixels[target + 2] = static_cast<uint8_t>((channels[2] + count / 2) / count);
            pixels[target + 3] = static_cast<uint8_t>((channels[3] + count / 2) / count);
        }
    }

    return pixels;
}

} // namespace
#endif

bool DawnBackend::ensureImageGlyphAtlasResources()
{
#if !LUTE_UI_USE_HARFBUZZ_GPU
    return true;
#else
    if (!imageAtlasTexture)
    {
        wgpu::TextureDescriptor textureDescriptor;
        textureDescriptor.size = {kImageAtlasSize, kImageAtlasSize, 1};
        textureDescriptor.format = wgpu::TextureFormat::RGBA8UnormSrgb;
        textureDescriptor.mipLevelCount = kImageAtlasMipLevels;
        textureDescriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        imageAtlasTexture = device.CreateTexture(&textureDescriptor);
        if (!imageAtlasTexture)
            return false;

        imageAtlasTextureView = imageAtlasTexture.CreateView();

        wgpu::SamplerDescriptor samplerDescriptor;
        samplerDescriptor.addressModeU = wgpu::AddressMode::ClampToEdge;
        samplerDescriptor.addressModeV = wgpu::AddressMode::ClampToEdge;
        samplerDescriptor.addressModeW = wgpu::AddressMode::ClampToEdge;
        samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.minFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Linear;
        samplerDescriptor.lodMinClamp = 0.0f;
        samplerDescriptor.lodMaxClamp = static_cast<float>(kImageAtlasMipLevels - 1);
        imageAtlasSampler = device.CreateSampler(&samplerDescriptor);

        imageGlyphCache.clear();
        imageAtlasCursorX = 0;
        imageAtlasCursorY = 0;
        imageAtlasRowHeight = 0;
    }

    if (!imageGlyphBindGroupLayout)
    {
        wgpu::BindGroupLayoutEntry entries[2];
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[0].texture.multisampled = false;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;

        wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor;
        bindGroupLayoutDescriptor.entryCount = 2;
        bindGroupLayoutDescriptor.entries = entries;
        imageGlyphBindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);
    }

    if (!imageGlyphBindGroup)
    {
        wgpu::BindGroupEntry bindGroupEntries[2];
        bindGroupEntries[0].binding = 0;
        bindGroupEntries[0].textureView = imageAtlasTextureView;
        bindGroupEntries[1].binding = 1;
        bindGroupEntries[1].sampler = imageAtlasSampler;

        wgpu::BindGroupDescriptor bindGroupDescriptor;
        bindGroupDescriptor.layout = imageGlyphBindGroupLayout;
        bindGroupDescriptor.entryCount = 2;
        bindGroupDescriptor.entries = bindGroupEntries;
        imageGlyphBindGroup = device.CreateBindGroup(&bindGroupDescriptor);
    }

    return imageGlyphBindGroup != nullptr;
#endif
}

#if LUTE_UI_USE_HARFBUZZ_GPU
const ImageGlyph* DawnBackend::lookupImageGlyph(const FontFace& fontFace, hb_codepoint_t glyph, uint32_t targetPpem)
{
    GlyphCacheKey key{&fontFace, glyph, targetPpem};
    auto found = imageGlyphCache.find(key);
    if (found != imageGlyphCache.end())
        return &found->second;

    ImageGlyph result;
    if (fontFace.harfbuzzFace() && fontFace.harfbuzzFont())
    {
        hb_font_t* pngFont = hb_font_create_sub_font(fontFace.harfbuzzFont());
        if (pngFont)
            hb_font_set_ppem(pngFont, targetPpem, targetPpem);
        hb_blob_t* image = hb_ot_color_glyph_reference_png(pngFont ? pngFont : fontFace.harfbuzzFont(), glyph);
        ImageGlyphBlobFormat format = ImageGlyphBlobFormat::Png;
        if (pngFont)
            hb_font_destroy(pngFont);
        unsigned int imageLength = image ? hb_blob_get_length(image) : 0;
        if ((!image || imageLength == 0) && hb_ot_color_has_svg(fontFace.harfbuzzFace()))
        {
            if (image)
                hb_blob_destroy(image);
            image = hb_ot_color_glyph_reference_svg(fontFace.harfbuzzFace(), glyph);
            format = ImageGlyphBlobFormat::Svg;
            imageLength = image ? hb_blob_get_length(image) : 0;
        }

        if (image && imageLength > 0)
        {
            std::optional<DecodedImage> decoded = decodeImageGlyphBlob(image, format);
            if (decoded)
                result = uploadImageGlyph(fontFace, glyph, *decoded);
        }

        if (image)
            hb_blob_destroy(image);
    }

    auto [inserted, _] = imageGlyphCache.emplace(key, result);
    return &inserted->second;
}

ImageGlyph DawnBackend::uploadImageGlyph(const FontFace& fontFace, hb_codepoint_t glyph, const DecodedImage& image)
{
    ImageGlyph result;
    if (!imageAtlasTexture || image.width == 0 || image.height == 0 || image.pixels.empty())
        return result;

    hb_glyph_extents_t extents = {};
    bool usedPlatformMetrics = false;
    GlyphMetrics platformMetrics = fontFace.glyphMetrics(glyph, kDefaultUiFontSize);
    if (fontFace.prefersPlatformGlyphMetrics() && platformMetrics.available && platformMetrics.width > 0.0f && platformMetrics.height != 0.0f)
    {
        float unitsPerPixel = static_cast<float>(fontFace.unitsPerEm()) / kDefaultUiFontSize;
        result.minX = platformMetrics.xBearing * unitsPerPixel;
        result.maxX = (platformMetrics.xBearing + platformMetrics.width) * unitsPerPixel;
        result.maxY = platformMetrics.yBearing * unitsPerPixel;
        result.minY = (platformMetrics.yBearing + platformMetrics.height) * unitsPerPixel;
        usedPlatformMetrics = true;
    }
    else if (!hb_font_get_glyph_extents(fontFace.harfbuzzFont(), glyph, &extents) || extents.width == 0 || extents.height == 0)
    {
        float upem = static_cast<float>(fontFace.unitsPerEm());
        float aspect = static_cast<float>(image.width) / static_cast<float>(std::max<uint32_t>(image.height, 1));
        extents.x_bearing = 0;
        extents.y_bearing = static_cast<hb_position_t>(upem * 0.8f);
        extents.width = static_cast<hb_position_t>(upem * aspect);
        extents.height = -static_cast<hb_position_t>(upem);
    }

    if (image.width > kImageAtlasSize - 2 * kImageAtlasGutter || image.height > kImageAtlasSize - 2 * kImageAtlasGutter)
        return result;

    ImageGlyphAtlasSlot slot;
    if (!allocateImageAtlas(image.width, image.height, slot))
    {
        if (!resetImageGlyphAtlas() || !allocateImageAtlas(image.width, image.height, slot))
            return result;
    }

    if (!uploadImageGlyphMipChain(slot, image))
        return result;

    if (!usedPlatformMetrics)
    {
        result.minX = static_cast<float>(extents.x_bearing);
        result.maxX = static_cast<float>(extents.x_bearing + extents.width);
        result.maxY = static_cast<float>(extents.y_bearing);
        result.minY = static_cast<float>(extents.y_bearing + extents.height);
    }

    result.u0 = (static_cast<float>(slot.x) + 0.5f) / static_cast<float>(kImageAtlasSize);
    result.v0 = (static_cast<float>(slot.y) + 0.5f) / static_cast<float>(kImageAtlasSize);
    result.u1 = (static_cast<float>(slot.x + slot.width) - 0.5f) / static_cast<float>(kImageAtlasSize);
    result.v1 = (static_cast<float>(slot.y + slot.height) - 0.5f) / static_cast<float>(kImageAtlasSize);
    result.empty = result.maxX <= result.minX || result.maxY <= result.minY;
    return result;
}

bool DawnBackend::allocateImageAtlas(uint32_t width, uint32_t height, ImageGlyphAtlasSlot& slot)
{
    if (width == 0 || height == 0 || width > kImageAtlasSize - 2 * kImageAtlasGutter || height > kImageAtlasSize - 2 * kImageAtlasGutter)
        return false;

    uint32_t uploadWidth = width + 2 * kImageAtlasGutter;
    uint32_t uploadHeight = height + 2 * kImageAtlasGutter;
    uint32_t atlasX = alignImageAtlasCoord(imageAtlasCursorX);
    uint32_t atlasY = imageAtlasCursorY;

    if (atlasX + uploadWidth > kImageAtlasSize)
    {
        atlasX = 0;
        atlasY = alignImageAtlasCoord(imageAtlasCursorY + imageAtlasRowHeight);
        imageAtlasCursorY = atlasY;
        imageAtlasRowHeight = 0;
    }

    if (atlasY + uploadHeight > kImageAtlasSize)
        return false;

    slot.uploadX = atlasX;
    slot.uploadY = atlasY;
    slot.uploadWidth = uploadWidth;
    slot.uploadHeight = uploadHeight;
    slot.x = atlasX + kImageAtlasGutter;
    slot.y = atlasY + kImageAtlasGutter;
    slot.width = width;
    slot.height = height;

    imageAtlasCursorX = atlasX + uploadWidth;
    imageAtlasRowHeight = std::max(imageAtlasRowHeight, uploadHeight);
    return true;
}

bool DawnBackend::uploadImageGlyphMipChain(const ImageGlyphAtlasSlot& slot, const DecodedImage& image)
{
    if (!imageAtlasTexture)
        return false;

    std::vector<uint8_t> pixels = makeGutteredImage(image);
    uint32_t width = slot.uploadWidth;
    uint32_t height = slot.uploadHeight;
    uint32_t x = slot.uploadX;
    uint32_t y = slot.uploadY;

    for (uint32_t level = 0; level < kImageAtlasMipLevels; level++)
    {
        wgpu::TexelCopyTextureInfo destination;
        destination.texture = imageAtlasTexture;
        destination.mipLevel = level;
        destination.origin = {x, y, 0};
        destination.aspect = wgpu::TextureAspect::All;

        wgpu::TexelCopyBufferLayout layout;
        layout.bytesPerRow = width * 4;
        layout.rowsPerImage = height;

        wgpu::Extent3D size{width, height, 1};
        {
            ProfileZone zone(ProfilePhase::Upload);
            queue.WriteTexture(&destination, pixels.data(), pixels.size(), &layout, &size);
            UiProfiler::addBufferUpload(pixels.size());
        }

        if (level + 1 >= kImageAtlasMipLevels)
            break;

        pixels = downsampleRgba(pixels, width, height);
        width = std::max(1u, (width + 1) / 2);
        height = std::max(1u, (height + 1) / 2);
        x /= 2;
        y /= 2;
    }

    return true;
}

bool DawnBackend::resetImageGlyphAtlas()
{
    imageAtlasTexture = {};
    imageAtlasTextureView = {};
    imageAtlasSampler = {};
    imageGlyphBindGroup = {};
    imageAtlasCursorX = 0;
    imageAtlasCursorY = 0;
    imageAtlasRowHeight = 0;
    imageGlyphCache.clear();
    return ensureImageGlyphAtlasResources();
}
#endif

} // namespace lute::ui::dawn
#endif
