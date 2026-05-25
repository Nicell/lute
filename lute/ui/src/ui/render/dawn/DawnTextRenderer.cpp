#include "DawnInternal.h"

#if LUTE_UI_USE_DAWN
#include <algorithm>
#include <cmath>
#include <cstring>

namespace lute::ui::dawn
{

void DawnBackend::appendTextRun(FrameVertices& vertices, const DisplayItem& item, float scale)
{
#if LUTE_UI_USE_HARFBUZZ_GPU
    if (item.text.empty() || !ensureGlyphEncoders())
        return;

    GlyphRun fallbackRun;
    const GlyphRun* run = item.glyphRun.get();
    if (!run)
    {
        TextShaper shaper;
        fallbackRun = shaper.shapeSingleRun(item.text);
        run = &fallbackRun;
    }
    if (run->glyphs.empty())
        return;

    UiProfiler::addTextRunRendered();
    UiProfiler::addGlyphsRendered(static_cast<uint32_t>(std::min<size_t>(run->glyphs.size(), UINT32_MAX)));

    float fontSize = run->fontSize * scale;
    float penX = item.origin.x * scale;
    float penY = std::floor(item.origin.y * scale);
    auto color = colorToLinearFloat(item.fill.color);
    std::array<float, 4> background = {0.0f, 0.0f, 0.0f, -1.0f};
    if (item.backgroundHint)
        background = colorToLinearFloat(*item.backgroundHint);

    for (const ShapedGlyph& shaped : run->glyphs)
    {
        const FontFace* glyphFont = shaped.fontFace ? shaped.fontFace : &defaultUiFontFace();
        uint32_t upem = glyphFont->unitsPerEm();
        float glyphX = penX + shaped.xOffset * scale;
        float glyphY = penY - shaped.yOffset * scale;

        uint32_t imagePpem = static_cast<uint32_t>(std::max(1.0f, std::ceil(fontSize)));
        if (glyphFont->prefersPlatformGlyphMetrics())
        {
            GlyphMetrics metrics = glyphFont->glyphMetrics(shaped.id, run->fontSize);
            if (metrics.available && metrics.height != 0.0f)
                imagePpem = static_cast<uint32_t>(std::max(1.0f, std::ceil(std::abs(metrics.height) * scale)));
        }

        const ImageGlyph* imageGlyph = lookupImageGlyph(*glyphFont, shaped.id, imagePpem);
        if (imageGlyph && !imageGlyph->empty)
        {
            appendImageGlyphVertices(vertices.imageGlyphs, glyphX, glyphY, fontSize, upem, *imageGlyph);
        }
        else
        {
            const EncodedGlyph* glyph = lookupVectorGlyph(*glyphFont, shaped.id);
            if (glyph)
            {
                std::vector<GlyphVertex>& target = glyph->renderMode == 1 ? vertices.paintGlyphs : vertices.drawGlyphs;
                appendGlyphVertices(target, glyphX, glyphY, fontSize, upem, *glyph, color, background);
            }
        }

        penX += shaped.xAdvance * scale;
        penY -= shaped.yAdvance * scale;
    }
#else
    (void)vertices;
    (void)item;
    (void)scale;
#endif
}

#if LUTE_UI_USE_HARFBUZZ_GPU
bool DawnBackend::ensureGlyphEncoders()
{
    if (draw && paint)
        return true;

    if (!draw)
        draw = hb_gpu_draw_create_or_fail();
    if (!paint)
        paint = hb_gpu_paint_create_or_fail();

    if (!draw || !paint)
        return false;

    return true;
}

const EncodedGlyph* DawnBackend::lookupVectorGlyph(const FontFace& fontFace, hb_codepoint_t glyph)
{
    GlyphCacheKey key{&fontFace, glyph, 0};
    auto found = glyphCache.find(key);
    if (found != glyphCache.end())
        return &found->second;

    if (!draw || !paint || !fontFace.harfbuzzFont())
        return nullptr;

    EncodedGlyph result;
    if (tryEncodePaintGlyph(fontFace, glyph, result) || tryEncodeDrawGlyph(fontFace, glyph, result))
    {
        auto [inserted, _] = glyphCache.emplace(key, result);
        return &inserted->second;
    }

    auto [inserted, _] = glyphCache.emplace(key, result);
    return &inserted->second;
}

bool DawnBackend::tryEncodePaintGlyph(const FontFace& fontFace, hb_codepoint_t glyph, EncodedGlyph& result)
{
    hb_face_t* face = fontFace.harfbuzzFace();
    if (!face || (!hb_ot_color_has_paint(face) && !hb_ot_color_has_layers(face)))
        return false;

    hb_gpu_paint_clear(paint);
    hb_gpu_paint_set_scale(paint, static_cast<int>(fontFace.unitsPerEm()), static_cast<int>(fontFace.unitsPerEm()));
    if (!hb_gpu_paint_glyph_or_fail(paint, fontFace.harfbuzzFont(), glyph))
        return false;

    hb_glyph_extents_t extents = {};
    hb_blob_t* encoded = hb_gpu_paint_encode(paint, &extents);
    bool ok = finishEncodedGlyph(encoded, extents, 1, result);

    if (encoded)
        hb_gpu_paint_recycle_blob(paint, encoded);

    return ok;
}

bool DawnBackend::tryEncodeDrawGlyph(const FontFace& fontFace, hb_codepoint_t glyph, EncodedGlyph& result)
{
    hb_gpu_draw_clear(draw);
    hb_gpu_draw_set_scale(draw, static_cast<int>(fontFace.unitsPerEm()), static_cast<int>(fontFace.unitsPerEm()));
    if (!hb_gpu_draw_glyph_or_fail(draw, fontFace.harfbuzzFont(), glyph))
        return false;

    hb_glyph_extents_t extents = {};
    hb_blob_t* encoded = hb_gpu_draw_encode(draw, &extents);
    bool ok = finishEncodedGlyph(encoded, extents, 0, result);

    if (encoded)
        hb_gpu_draw_recycle_blob(draw, encoded);

    return ok;
}

bool DawnBackend::finishEncodedGlyph(hb_blob_t* encoded, const hb_glyph_extents_t& extents, uint32_t renderMode, EncodedGlyph& result)
{
    unsigned int encodedLength = encoded ? hb_blob_get_length(encoded) : 0;
    result.minX = static_cast<float>(extents.x_bearing);
    result.maxX = static_cast<float>(extents.x_bearing + extents.width);
    result.maxY = static_cast<float>(extents.y_bearing);
    result.minY = static_cast<float>(extents.y_bearing + extents.height);
    result.renderMode = renderMode;
    result.empty = encodedLength == 0;

    if (result.empty)
        return false;

    const char* encodedData = hb_blob_get_data(encoded, &encodedLength);
    if (!encodedData || !appendAtlas(encodedData, encodedLength, result.atlasOffset))
    {
        result.empty = true;
        return false;
    }

    return true;
}

bool DawnBackend::appendAtlas(const char* data, unsigned int length, uint32_t& offset)
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
    markAtlasDirtyRange(offset, texels);
    return true;
}

void DawnBackend::markAtlasDirtyRange(uint64_t offset, uint64_t texels)
{
    if (texels == 0)
        return;

    atlasDirtyStart = std::min(atlasDirtyStart, offset);
    atlasDirtyEnd = std::max(atlasDirtyEnd, offset + texels);
}

void DawnBackend::clearAtlasDirtyRange()
{
    atlasDirtyStart = kAtlasCapacity;
    atlasDirtyEnd = 0;
}
#endif

} // namespace lute::ui::dawn
#endif
