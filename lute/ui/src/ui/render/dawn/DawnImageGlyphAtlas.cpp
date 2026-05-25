#include "DawnInternal.h"

#if LUTE_UI_USE_DAWN
#include <algorithm>
#include <cmath>

namespace lute::ui::dawn
{

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
        imageAtlasSampler = device.CreateSampler(&samplerDescriptor);

        imageGlyphCache.clear();
        imageAtlasCursorX = 0;
        imageAtlasCursorY = 0;
        imageAtlasRowHeight = 0;
    }

    if (!imageGlyphBindGroup)
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
        if (pngFont)
            hb_font_destroy(pngFont);
        unsigned int imageLength = image ? hb_blob_get_length(image) : 0;
        if ((!image || imageLength == 0) && hb_ot_color_has_svg(fontFace.harfbuzzFace()))
        {
            if (image)
                hb_blob_destroy(image);
            image = hb_ot_color_glyph_reference_svg(fontFace.harfbuzzFace(), glyph);
            imageLength = image ? hb_blob_get_length(image) : 0;
        }

        if (image && imageLength > 0)
        {
            std::optional<DecodedImage> decoded = decodeImageBlob(image);
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

    uint32_t atlasX = 0;
    uint32_t atlasY = 0;
    if (!allocateImageAtlas(image.width, image.height, atlasX, atlasY))
        return result;

    wgpu::TexelCopyTextureInfo destination;
    destination.texture = imageAtlasTexture;
    destination.origin = {atlasX, atlasY, 0};
    destination.aspect = wgpu::TextureAspect::All;

    wgpu::TexelCopyBufferLayout layout;
    layout.bytesPerRow = image.width * 4;
    layout.rowsPerImage = image.height;

    wgpu::Extent3D size{image.width, image.height, 1};
    {
        ProfileZone zone(ProfilePhase::Upload);
        queue.WriteTexture(&destination, image.pixels.data(), image.pixels.size(), &layout, &size);
        UiProfiler::addBufferUpload(image.pixels.size());
    }

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

    if (!usedPlatformMetrics)
    {
        result.minX = static_cast<float>(extents.x_bearing);
        result.maxX = static_cast<float>(extents.x_bearing + extents.width);
        result.maxY = static_cast<float>(extents.y_bearing);
        result.minY = static_cast<float>(extents.y_bearing + extents.height);
    }

    result.u0 = (static_cast<float>(atlasX) + 0.5f) / static_cast<float>(kImageAtlasSize);
    result.v0 = (static_cast<float>(atlasY) + 0.5f) / static_cast<float>(kImageAtlasSize);
    result.u1 = (static_cast<float>(atlasX + image.width) - 0.5f) / static_cast<float>(kImageAtlasSize);
    result.v1 = (static_cast<float>(atlasY + image.height) - 0.5f) / static_cast<float>(kImageAtlasSize);
    result.empty = result.maxX <= result.minX || result.maxY <= result.minY;
    return result;
}

bool DawnBackend::allocateImageAtlas(uint32_t width, uint32_t height, uint32_t& x, uint32_t& y)
{
    if (width + kImageAtlasPadding > kImageAtlasSize || height + kImageAtlasPadding > kImageAtlasSize)
        return false;

    if (imageAtlasCursorX + width + kImageAtlasPadding > kImageAtlasSize)
    {
        imageAtlasCursorX = 0;
        imageAtlasCursorY += imageAtlasRowHeight + kImageAtlasPadding;
        imageAtlasRowHeight = 0;
    }

    if (imageAtlasCursorY + height + kImageAtlasPadding > kImageAtlasSize)
        return false;

    x = imageAtlasCursorX;
    y = imageAtlasCursorY;
    imageAtlasCursorX += width + kImageAtlasPadding;
    imageAtlasRowHeight = std::max(imageAtlasRowHeight, height);
    return true;
}
#endif

} // namespace lute::ui::dawn
#endif
