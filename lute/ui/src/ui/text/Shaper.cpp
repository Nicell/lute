#include "lute/ui/Text.h"

#if LUTE_UI_USE_HARFBUZZ
#include "hb.h"
#include "hb-ot.h"
#endif

#if defined(__APPLE__) && LUTE_UI_USE_HARFBUZZ
#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#endif

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace lute::ui
{

namespace
{

struct ResolvedFont
{
    std::string path;
    std::string postScriptName;
};

#if defined(__APPLE__) && LUTE_UI_USE_HARFBUZZ
static std::string cfStringToUtf8(CFStringRef string)
{
    if (!string)
        return {};

    CFIndex length = CFStringGetLength(string);
    CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string result(static_cast<size_t>(maxSize), '\0');
    if (!CFStringGetCString(string, result.data(), maxSize, kCFStringEncodingUTF8))
        return {};

    result.resize(std::char_traits<char>::length(result.c_str()));
    return result;
}

static std::string cfUrlPath(CFURLRef url)
{
    if (!url)
        return {};

    UInt8 buffer[PATH_MAX];
    if (!CFURLGetFileSystemRepresentation(url, true, buffer, sizeof(buffer)))
        return {};

    return reinterpret_cast<const char*>(buffer);
}

static bool resolveCoreTextSystemFont(ResolvedFont& result)
{
    CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 0.0, nullptr);
    if (!font)
        font = CTFontCreateWithName(CFSTR(".AppleSystemUIFont"), 0.0, nullptr);
    if (!font)
        return false;

    CFStringRef postScriptName = CTFontCopyPostScriptName(font);
    if (postScriptName)
    {
        result.postScriptName = cfStringToUtf8(postScriptName);
        CFRelease(postScriptName);
    }

    CFTypeRef urlValue = CTFontCopyAttribute(font, kCTFontURLAttribute);
    if (urlValue && CFGetTypeID(urlValue) == CFURLGetTypeID())
        result.path = cfUrlPath(static_cast<CFURLRef>(urlValue));

    if (urlValue)
        CFRelease(urlValue);

    if (result.path.empty())
    {
        CTFontDescriptorRef descriptor = CTFontCopyFontDescriptor(font);
        if (descriptor)
        {
            CFTypeRef descriptorUrl = CTFontDescriptorCopyAttribute(descriptor, kCTFontURLAttribute);
            if (descriptorUrl && CFGetTypeID(descriptorUrl) == CFURLGetTypeID())
                result.path = cfUrlPath(static_cast<CFURLRef>(descriptorUrl));
            if (descriptorUrl)
                CFRelease(descriptorUrl);
            CFRelease(descriptor);
        }
    }

    CFRelease(font);
    return !result.path.empty();
}
#endif

static bool resolveFallbackFont(ResolvedFont& result)
{
    static constexpr const char* fontPaths[] = {
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/SFCompact.ttf",
        "/System/Library/Fonts/HelveticaNeue.ttc",
        "/System/Library/Fonts/Geneva.ttf",
    };

    for (const char* path : fontPaths)
    {
#if LUTE_UI_USE_HARFBUZZ
        hb_blob_t* probeBlob = hb_blob_create_from_file_or_fail(path);
        if (!probeBlob)
            continue;

        bool valid = hb_blob_get_length(probeBlob) > 0;
        hb_blob_destroy(probeBlob);
        if (!valid)
            continue;
#endif

        result.path = path;
        return true;
    }

    return false;
}

#if LUTE_UI_USE_HARFBUZZ
static std::string facePostScriptName(hb_face_t* face)
{
    if (!face)
        return {};

    unsigned int size = 0;
    hb_ot_name_get_utf8(face, HB_OT_NAME_ID_POSTSCRIPT_NAME, HB_LANGUAGE_INVALID, &size, nullptr);
    if (size == 0)
        return {};

    std::string result(size, '\0');
    unsigned int written = size;
    hb_ot_name_get_utf8(face, HB_OT_NAME_ID_POSTSCRIPT_NAME, HB_LANGUAGE_INVALID, &written, result.data());
    result.resize(written);
    return result;
}

static uint32_t findFaceIndex(hb_blob_t* blob, const std::string& postScriptName)
{
    if (!blob || postScriptName.empty())
        return 0;

    unsigned int faceCount = hb_face_count(blob);
    for (unsigned int i = 0; i < faceCount; i++)
    {
        hb_face_t* candidate = hb_face_create(blob, i);
        std::string candidateName = facePostScriptName(candidate);
        hb_face_destroy(candidate);

        if (candidateName == postScriptName)
            return i;
    }

    return 0;
}
#endif

static FontMetrics fallbackMetrics(float fontSize)
{
    return {
        fontSize * 0.8f,
        fontSize * 0.2f,
        0.0f,
        std::max(20.0f, std::ceil(fontSize * 1.25f)),
        std::max(15.0f, std::ceil(fontSize * 0.8f)),
    };
}

} // namespace

FontFace::FontFace()
{
    loaded = loadDefault();
}

FontFace::~FontFace()
{
#if LUTE_UI_USE_HARFBUZZ
    hb_font_destroy(font);
    hb_face_destroy(face);
    hb_blob_destroy(blob);
#endif
}

bool FontFace::available() const
{
    return loaded;
}

const std::string& FontFace::path() const
{
    return fontPath;
}

const std::string& FontFace::postScriptName() const
{
    return fontPostScriptName;
}

uint32_t FontFace::faceIndex() const
{
    return fontFaceIndex;
}

uint32_t FontFace::unitsPerEm() const
{
    return fontUnitsPerEm;
}

FontMetrics FontFace::metrics(float fontSize) const
{
    if (!available() || fontUnitsPerEm == 0)
        return fallbackMetrics(fontSize);

    float scale = fontSize / static_cast<float>(fontUnitsPerEm);
    float ascender = std::max(0.0f, static_cast<float>(fontAscender) * scale);
    float descender = std::max(0.0f, static_cast<float>(-fontDescender) * scale);
    float lineGap = std::max(0.0f, static_cast<float>(fontLineGap) * scale);
    float naturalLineHeight = ascender + descender + lineGap;

    FontMetrics result;
    result.ascender = ascender;
    result.descender = descender;
    result.lineGap = lineGap;
    result.lineHeight = std::ceil(std::max(naturalLineHeight, fontSize));
    result.baseline = std::ceil(ascender);
    return result;
}

hb_font_t* FontFace::harfbuzzFont() const
{
    return font;
}

bool FontFace::loadDefault()
{
    ResolvedFont resolved;
#if defined(__APPLE__) && LUTE_UI_USE_HARFBUZZ
    resolveCoreTextSystemFont(resolved);
#endif

    if (resolved.path.empty() && !resolveFallbackFont(resolved))
        return false;

    return loadFromPath(std::move(resolved.path), std::move(resolved.postScriptName));
}

bool FontFace::loadFromPath(std::string nextPath, std::string nextPostScriptName)
{
#if LUTE_UI_USE_HARFBUZZ
    hb_blob_t* nextBlob = hb_blob_create_from_file_or_fail(nextPath.c_str());
    if (!nextBlob)
        return false;

    uint32_t nextFaceIndex = findFaceIndex(nextBlob, nextPostScriptName);
    hb_face_t* nextFace = hb_face_create(nextBlob, nextFaceIndex);
    if (!nextFace || hb_face_get_glyph_count(nextFace) == 0)
    {
        hb_face_destroy(nextFace);
        hb_blob_destroy(nextBlob);
        return false;
    }

    hb_font_t* nextFont = hb_font_create(nextFace);
    hb_ot_font_set_funcs(nextFont);

    uint32_t nextUnitsPerEm = hb_face_get_upem(nextFace);
    if (nextUnitsPerEm == 0)
        nextUnitsPerEm = 1000;

    hb_font_set_scale(nextFont, static_cast<int>(nextUnitsPerEm), static_cast<int>(nextUnitsPerEm));

    hb_font_extents_t extents = {};
    if (!hb_font_get_h_extents(nextFont, &extents))
    {
        extents.ascender = static_cast<hb_position_t>(nextUnitsPerEm * 0.8f);
        extents.descender = -static_cast<hb_position_t>(nextUnitsPerEm * 0.2f);
        extents.line_gap = 0;
    }

    blob = nextBlob;
    face = nextFace;
    font = nextFont;
    fontPath = std::move(nextPath);
    fontPostScriptName = std::move(nextPostScriptName);
    fontFaceIndex = nextFaceIndex;
    fontUnitsPerEm = nextUnitsPerEm;
    fontAscender = extents.ascender;
    fontDescender = extents.descender;
    fontLineGap = extents.line_gap;
    return true;
#else
    (void)nextPath;
    (void)nextPostScriptName;
    return false;
#endif
}

const FontFace& defaultUiFontFace()
{
    static FontFace font;
    return font;
}

GlyphRun TextShaper::shapeSingleRun(const std::string& utf8, float fontSize) const
{
#if LUTE_UI_USE_HARFBUZZ
    const FontFace& fontFace = defaultUiFontFace();
    if (!fontFace.available() || !fontFace.harfbuzzFont())
        return {utf8, {}, static_cast<float>(utf8.size()) * 8.0f, fontSize, fallbackMetrics(fontSize), false};

    int length = static_cast<int>(std::min<size_t>(utf8.size(), static_cast<size_t>(std::numeric_limits<int>::max())));
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, utf8.data(), length, 0, length);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(fontFace.harfbuzzFont(), buffer, nullptr, 0);

    unsigned glyphCount = 0;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyphCount);

    float unitToPixel = fontSize / static_cast<float>(fontFace.unitsPerEm());
    std::vector<ShapedGlyph> glyphs;
    glyphs.reserve(glyphCount);

    float advance = 0.0f;
    for (unsigned i = 0; i < glyphCount; i++)
    {
        ShapedGlyph glyph;
        glyph.id = infos[i].codepoint;
        glyph.xAdvance = static_cast<float>(positions[i].x_advance) * unitToPixel;
        glyph.yAdvance = static_cast<float>(positions[i].y_advance) * unitToPixel;
        glyph.xOffset = static_cast<float>(positions[i].x_offset) * unitToPixel;
        glyph.yOffset = static_cast<float>(positions[i].y_offset) * unitToPixel;
        advance += glyph.xAdvance;
        glyphs.push_back(glyph);
    }

    bool rtl = HB_DIRECTION_IS_BACKWARD(hb_buffer_get_direction(buffer));

    hb_buffer_destroy(buffer);

    if (advance <= 0.0f)
        advance = static_cast<float>(utf8.size()) * 8.0f;

    return {utf8, std::move(glyphs), advance, fontSize, fontFace.metrics(fontSize), rtl};
#else
    bool rtl = false;
    for (unsigned char byte : utf8)
    {
        if (byte >= 0xd6 && byte <= 0xef)
        {
            rtl = true;
            break;
        }
    }

    return {utf8, {}, static_cast<float>(utf8.size()) * 8.0f, fontSize, fallbackMetrics(fontSize), rtl};
#endif
}

MeasureResult TextShaper::measureSingleLine(const std::string& utf8, float fontSize) const
{
    GlyphRun run = shapeSingleRun(utf8, fontSize);
    return {{run.advance, run.metrics.lineHeight}, run.metrics.baseline, run.metrics.baseline};
}

} // namespace lute::ui
