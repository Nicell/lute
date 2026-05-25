#include "lute/ui/Text.h"

#if LUTE_UI_USE_HARFBUZZ
#include "hb.h"
#include "hb-ot.h"
#endif

#if defined(__APPLE__) && LUTE_UI_USE_HARFBUZZ
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#endif

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lute::ui
{

namespace
{

struct ResolvedFont
{
    std::string path;
    std::string postScriptName;
    std::shared_ptr<const void> platformFont;
    float platformAscenderRatio = 0.0f;
    float platformDescenderRatio = 0.0f;
    float platformLineGapRatio = 0.0f;
    std::vector<FontVariation> platformVariations;
};

#if defined(__APPLE__) && LUTE_UI_USE_HARFBUZZ
static std::shared_ptr<const void> retainCoreTextFont(CTFontRef font)
{
    if (!font)
        return {};

    CFRetain(font);
    return std::shared_ptr<const void>(font, [](const void* value) {
        if (value)
            CFRelease(value);
    });
}

static CTFontRef createCoreTextSystemFont(float fontSize = 0.0f)
{
    CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, fontSize, nullptr);
    if (!font)
        font = CTFontCreateWithName(CFSTR(".AppleSystemUIFont"), fontSize, nullptr);
    return font;
}

static void collectCoreTextVariation(const void* key, const void* value, void* context)
{
    auto* variations = static_cast<std::vector<FontVariation>*>(context);
    if (!key || !value || CFGetTypeID(key) != CFNumberGetTypeID() || CFGetTypeID(value) != CFNumberGetTypeID())
        return;

    int32_t tag = 0;
    double axisValue = 0.0;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(key), kCFNumberSInt32Type, &tag) ||
        !CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, &axisValue))
    {
        return;
    }

    variations->push_back({static_cast<uint32_t>(tag), static_cast<float>(axisValue)});
}

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

static ResolvedFont resolvedFontFromCoreTextFont(CTFontRef font)
{
    ResolvedFont result;
    if (!font)
        return result;

    CGFloat size = CTFontGetSize(font);
    if (size > 0.0)
    {
        result.platformFont = retainCoreTextFont(font);
        result.platformAscenderRatio = static_cast<float>(CTFontGetAscent(font) / size);
        result.platformDescenderRatio = static_cast<float>(CTFontGetDescent(font) / size);
        result.platformLineGapRatio = static_cast<float>(CTFontGetLeading(font) / size);
    }

    CFDictionaryRef variations = CTFontCopyVariation(font);
    if (variations)
    {
        CFDictionaryApplyFunction(variations, collectCoreTextVariation, &result.platformVariations);
        CFRelease(variations);
    }

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

    return result;
}

static bool resolveCoreTextSystemFont(ResolvedFont& result)
{
    CTFontRef font = createCoreTextSystemFont();
    if (!font)
        return false;

    result = resolvedFontFromCoreTextFont(font);
    CFRelease(font);
    return !result.path.empty();
}

static bool resolveCoreTextFallbackFont(std::string_view utf8, ResolvedFont& result)
{
    CTFontRef baseFont = createCoreTextSystemFont(kDefaultUiFontSize);
    if (!baseFont)
        return false;

    CFStringRef string = CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(utf8.data()),
        static_cast<CFIndex>(utf8.size()),
        kCFStringEncodingUTF8,
        false
    );
    if (!string)
    {
        CFRelease(baseFont);
        return false;
    }

    CTFontRef fallbackFont = CTFontCreateForString(baseFont, string, CFRangeMake(0, CFStringGetLength(string)));
    result = resolvedFontFromCoreTextFont(fallbackFont ? fallbackFont : baseFont);

    if (fallbackFont)
        CFRelease(fallbackFont);
    CFRelease(string);
    CFRelease(baseFont);
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
    float naturalLineHeight = fontSize;
    float lineHeight = std::ceil(std::max(naturalLineHeight, fontSize * (20.0f / kDefaultUiFontSize)));
    float baseline = (lineHeight - naturalLineHeight) * 0.5f + fontSize * 0.8f;
    return {
        fontSize * 0.8f,
        fontSize * 0.2f,
        0.0f,
        lineHeight,
        baseline,
    };
}

struct CodepointSpan
{
    uint32_t codepoint = 0;
    size_t start = 0;
    size_t end = 0;
};

struct TextCluster
{
    size_t start = 0;
    size_t end = 0;
    uint32_t firstCodepoint = 0;
};

struct FontRun
{
    size_t start = 0;
    size_t end = 0;
    const FontFace* fontFace = nullptr;
};

static uint32_t decodeUtf8Codepoint(const std::string& text, size_t& offset)
{
    unsigned char first = static_cast<unsigned char>(text[offset]);
    size_t remaining = text.size() - offset;

    auto consumeInvalid = [&]() {
        offset++;
        return static_cast<uint32_t>(first);
    };

    if (first < 0x80)
    {
        offset++;
        return first;
    }

    if ((first & 0xe0) == 0xc0 && remaining >= 2)
    {
        unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        if ((b1 & 0xc0) != 0x80)
            return consumeInvalid();
        offset += 2;
        return ((first & 0x1f) << 6) | (b1 & 0x3f);
    }

    if ((first & 0xf0) == 0xe0 && remaining >= 3)
    {
        unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
        if ((b1 & 0xc0) != 0x80 || (b2 & 0xc0) != 0x80)
            return consumeInvalid();
        offset += 3;
        return ((first & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f);
    }

    if ((first & 0xf8) == 0xf0 && remaining >= 4)
    {
        unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
        unsigned char b3 = static_cast<unsigned char>(text[offset + 3]);
        if ((b1 & 0xc0) != 0x80 || (b2 & 0xc0) != 0x80 || (b3 & 0xc0) != 0x80)
            return consumeInvalid();
        offset += 4;
        return ((first & 0x07) << 18) | ((b1 & 0x3f) << 12) | ((b2 & 0x3f) << 6) | (b3 & 0x3f);
    }

    return consumeInvalid();
}

static std::vector<CodepointSpan> codepointSpans(const std::string& text)
{
    std::vector<CodepointSpan> result;
    size_t offset = 0;
    while (offset < text.size())
    {
        size_t start = offset;
        uint32_t codepoint = decodeUtf8Codepoint(text, offset);
        result.push_back({codepoint, start, offset});
    }
    return result;
}

static bool isVariationSelector(uint32_t codepoint)
{
    return (codepoint >= 0xfe00 && codepoint <= 0xfe0f) || (codepoint >= 0xe0100 && codepoint <= 0xe01ef);
}

static bool isCombiningMark(uint32_t codepoint)
{
    return (codepoint >= 0x0300 && codepoint <= 0x036f) || (codepoint >= 0x1ab0 && codepoint <= 0x1aff) ||
        (codepoint >= 0x1dc0 && codepoint <= 0x1dff) || (codepoint >= 0x20d0 && codepoint <= 0x20ff) ||
        (codepoint >= 0xfe20 && codepoint <= 0xfe2f);
}

static bool isEmojiModifier(uint32_t codepoint)
{
    return codepoint >= 0x1f3fb && codepoint <= 0x1f3ff;
}

static bool isEmojiTag(uint32_t codepoint)
{
    return codepoint >= 0xe0020 && codepoint <= 0xe007f;
}

static bool isRegionalIndicator(uint32_t codepoint)
{
    return codepoint >= 0x1f1e6 && codepoint <= 0x1f1ff;
}

static bool isClusterExtender(uint32_t codepoint)
{
    return isVariationSelector(codepoint) || isCombiningMark(codepoint) || isEmojiModifier(codepoint) || isEmojiTag(codepoint) || codepoint == 0x20e3;
}

static std::vector<TextCluster> textClusters(const std::string& text)
{
    std::vector<CodepointSpan> spans = codepointSpans(text);
    std::vector<TextCluster> result;

    size_t i = 0;
    while (i < spans.size())
    {
        size_t start = spans[i].start;
        uint32_t first = spans[i].codepoint;
        i++;

        auto consumeExtenders = [&]() {
            while (i < spans.size() && isClusterExtender(spans[i].codepoint))
                i++;
        };

        consumeExtenders();

        if (isRegionalIndicator(first) && i < spans.size() && isRegionalIndicator(spans[i].codepoint))
        {
            i++;
            consumeExtenders();
        }

        while (i < spans.size() && spans[i].codepoint == 0x200d)
        {
            i++;
            if (i < spans.size())
            {
                i++;
                consumeExtenders();
            }
        }

        result.push_back({start, spans[i - 1].end, first});
    }

    return result;
}

static const FontFace& fontFaceForResolvedFont(const ResolvedFont& resolved)
{
    const FontFace& defaultFace = defaultUiFontFace();
    if (resolved.path.empty())
        return defaultFace;

    if (resolved.path == defaultFace.path() && (resolved.postScriptName.empty() || resolved.postScriptName == defaultFace.postScriptName()))
        return defaultFace;

    static std::unordered_map<std::string, std::unique_ptr<FontFace>> faces;
    std::string key = resolved.path + "#" + resolved.postScriptName;
    auto found = faces.find(key);
    if (found != faces.end())
        return *found->second;

    auto face = std::make_unique<FontFace>(
        resolved.path,
        resolved.postScriptName,
        resolved.platformFont,
        resolved.platformAscenderRatio,
        resolved.platformDescenderRatio,
        resolved.platformLineGapRatio,
        resolved.platformVariations
    );
    if (!face->available())
        return defaultFace;

    auto [inserted, _] = faces.emplace(std::move(key), std::move(face));
    return *inserted->second;
}

static std::vector<FontRun> fallbackFontRuns(const std::string& utf8)
{
    const FontFace& defaultFace = defaultUiFontFace();
    std::vector<FontRun> runs;
    if (utf8.empty())
        return runs;

#if defined(__APPLE__) && LUTE_UI_USE_HARFBUZZ
    std::vector<TextCluster> clusters = textClusters(utf8);
    for (const TextCluster& cluster : clusters)
    {
        std::string_view clusterText(utf8.data() + cluster.start, cluster.end - cluster.start);
        ResolvedFont resolved;
        const FontFace* face = &defaultFace;
        if (resolveCoreTextFallbackFont(clusterText, resolved))
            face = &fontFaceForResolvedFont(resolved);

        if (runs.empty() || runs.back().fontFace != face || runs.back().end != cluster.start)
            runs.push_back({cluster.start, cluster.end, face});
        else
            runs.back().end = cluster.end;
    }
#else
    runs.push_back({0, utf8.size(), &defaultFace});
#endif

    return runs;
}

static void mergeMetrics(FontMetrics& target, const FontMetrics& next, bool& initialized)
{
    if (!initialized)
    {
        target = next;
        initialized = true;
        return;
    }

    target.ascender = std::max(target.ascender, next.ascender);
    target.descender = std::max(target.descender, next.descender);
    target.lineGap = std::max(target.lineGap, next.lineGap);
    target.lineHeight = std::max(target.lineHeight, next.lineHeight);
    target.baseline = std::max(target.baseline, next.baseline);
}

static FontMetrics makeFontMetrics(float fontSize, float ascender, float descender, float lineGap)
{
    float naturalLineHeight = ascender + descender + lineGap;
    float preferredLineHeight = std::ceil(std::max(naturalLineHeight, fontSize * (20.0f / kDefaultUiFontSize)));
    float extraLeading = std::max(0.0f, preferredLineHeight - naturalLineHeight) * 0.5f;

    FontMetrics result;
    result.ascender = ascender;
    result.descender = descender;
    result.lineGap = lineGap;
    result.lineHeight = preferredLineHeight;
    result.baseline = extraLeading + ascender;
    return result;
}

static GlyphRun shapeWithFont(const std::string& utf8, const FontFace& fontFace, float fontSize)
{
#if LUTE_UI_USE_HARFBUZZ
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
        glyph.fontFace = &fontFace;
        glyph.id = infos[i].codepoint;
        glyph.xAdvance = static_cast<float>(positions[i].x_advance) * unitToPixel;
        glyph.yAdvance = static_cast<float>(positions[i].y_advance) * unitToPixel;
        glyph.xOffset = static_cast<float>(positions[i].x_offset) * unitToPixel;
        glyph.yOffset = static_cast<float>(positions[i].y_offset) * unitToPixel;
        if (fontFace.prefersPlatformGlyphMetrics())
        {
            GlyphMetrics metrics = fontFace.glyphMetrics(glyph.id, fontSize);
            if (metrics.available && metrics.xAdvance > 0.0f)
            {
                glyph.xAdvance = metrics.xAdvance;
                glyph.yAdvance = metrics.yAdvance;
            }
        }
        advance += glyph.xAdvance;
        glyphs.push_back(glyph);
    }

    bool rtl = HB_DIRECTION_IS_BACKWARD(hb_buffer_get_direction(buffer));

    hb_buffer_destroy(buffer);

    if (advance <= 0.0f)
        advance = static_cast<float>(utf8.size()) * 8.0f;

    return {utf8, std::move(glyphs), advance, fontSize, fontFace.metrics(fontSize), rtl};
#else
    (void)fontFace;
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

} // namespace

FontFace::FontFace()
{
    loaded = loadDefault();
}

FontFace::FontFace(std::string path, std::string postScriptName)
{
    loaded = loadFromPath(std::move(path), std::move(postScriptName));
}

FontFace::FontFace(
    std::string path,
    std::string postScriptName,
    std::shared_ptr<const void> retainedPlatformFont,
    float retainedPlatformAscenderRatio,
    float retainedPlatformDescenderRatio,
    float retainedPlatformLineGapRatio,
    std::vector<FontVariation> retainedPlatformVariations
)
{
    loaded = loadFromPath(
        std::move(path),
        std::move(postScriptName),
        std::move(retainedPlatformFont),
        retainedPlatformAscenderRatio,
        retainedPlatformDescenderRatio,
        retainedPlatformLineGapRatio,
        std::move(retainedPlatformVariations)
    );
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
    if (platformMetricsAvailable)
    {
        float ascender = std::max(0.0f, platformAscenderRatio * fontSize);
        float descender = std::max(0.0f, platformDescenderRatio * fontSize);
        float lineGap = std::max(0.0f, platformLineGapRatio * fontSize);
        return makeFontMetrics(fontSize, ascender, descender, lineGap);
    }

    if (!available() || fontUnitsPerEm == 0)
        return fallbackMetrics(fontSize);

    float scale = fontSize / static_cast<float>(fontUnitsPerEm);
    float ascender = std::max(0.0f, static_cast<float>(fontAscender) * scale);
    float descender = std::max(0.0f, static_cast<float>(-fontDescender) * scale);
    float lineGap = std::max(0.0f, static_cast<float>(fontLineGap) * scale);
    return makeFontMetrics(fontSize, ascender, descender, lineGap);
}

GlyphMetrics FontFace::glyphMetrics(uint32_t glyph, float fontSize) const
{
    GlyphMetrics result;
#if defined(__APPLE__) && LUTE_UI_USE_HARFBUZZ
    if (!platformFont || glyph > std::numeric_limits<CGGlyph>::max() || fontSize <= 0.0f)
        return result;

    CTFontRef font = static_cast<CTFontRef>(const_cast<void*>(platformFont.get()));
    CTFontRef sizedFont = CTFontCreateCopyWithAttributes(font, static_cast<CGFloat>(fontSize), nullptr, nullptr);
    if (!sizedFont)
        return result;

    CGGlyph cgGlyph = static_cast<CGGlyph>(glyph);
    CGSize advance = CGSizeZero;
    CGRect bounds = CGRectZero;
    double advanceWidth = CTFontGetAdvancesForGlyphs(sizedFont, kCTFontOrientationHorizontal, &cgGlyph, &advance, 1);
    CGRect boundingRect = CTFontGetBoundingRectsForGlyphs(sizedFont, kCTFontOrientationHorizontal, &cgGlyph, &bounds, 1);
    CFRelease(sizedFont);

    if (advanceWidth <= 0.0 && CGRectIsEmpty(boundingRect))
        return result;

    result.available = true;
    result.xAdvance = static_cast<float>(advance.width);
    result.yAdvance = static_cast<float>(advance.height);
    result.xBearing = static_cast<float>(bounds.origin.x);
    result.yBearing = static_cast<float>(bounds.origin.y + bounds.size.height);
    result.width = static_cast<float>(bounds.size.width);
    result.height = -static_cast<float>(bounds.size.height);
#else
    (void)glyph;
    (void)fontSize;
#endif
    return result;
}

bool FontFace::prefersPlatformGlyphMetrics() const
{
    return preferPlatformGlyphMetrics;
}

hb_face_t* FontFace::harfbuzzFace() const
{
    return face;
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

    return loadFromPath(
        std::move(resolved.path),
        std::move(resolved.postScriptName),
        std::move(resolved.platformFont),
        resolved.platformAscenderRatio,
        resolved.platformDescenderRatio,
        resolved.platformLineGapRatio,
        std::move(resolved.platformVariations)
    );
}

bool FontFace::loadFromPath(
    std::string nextPath,
    std::string nextPostScriptName,
    std::shared_ptr<const void> nextPlatformFont,
    float nextPlatformAscenderRatio,
    float nextPlatformDescenderRatio,
    float nextPlatformLineGapRatio,
    std::vector<FontVariation> nextPlatformVariations
)
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
    hb_font_set_ppem(nextFont, static_cast<unsigned int>(std::ceil(kDefaultUiFontSize)), static_cast<unsigned int>(std::ceil(kDefaultUiFontSize)));
    if (!nextPlatformVariations.empty())
    {
        std::vector<hb_variation_t> variations;
        variations.reserve(nextPlatformVariations.size());
        for (const FontVariation& variation : nextPlatformVariations)
            variations.push_back({variation.tag, variation.value});
        hb_font_set_variations(nextFont, variations.data(), static_cast<unsigned int>(variations.size()));
    }

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
    platformFont = std::move(nextPlatformFont);
    fontFaceIndex = nextFaceIndex;
    fontUnitsPerEm = nextUnitsPerEm;
    fontAscender = extents.ascender;
    fontDescender = extents.descender;
    fontLineGap = extents.line_gap;
    platformAscenderRatio = nextPlatformAscenderRatio;
    platformDescenderRatio = nextPlatformDescenderRatio;
    platformLineGapRatio = nextPlatformLineGapRatio;
    platformMetricsAvailable = platformFont && platformAscenderRatio > 0.0f;
    preferPlatformGlyphMetrics = platformFont && (hb_ot_color_has_png(nextFace) || hb_ot_color_has_svg(nextFace));
    return true;
#else
    (void)nextPath;
    (void)nextPostScriptName;
    (void)nextPlatformFont;
    (void)nextPlatformAscenderRatio;
    (void)nextPlatformDescenderRatio;
    (void)nextPlatformLineGapRatio;
    (void)nextPlatformVariations;
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

    GlyphRun result;
    result.text = utf8;
    result.fontSize = fontSize;

    bool hasMetrics = false;
    for (const FontRun& fontRun : fallbackFontRuns(utf8))
    {
        const FontFace* runFont = fontRun.fontFace ? fontRun.fontFace : &fontFace;
        GlyphRun shaped = shapeWithFont(utf8.substr(fontRun.start, fontRun.end - fontRun.start), *runFont, fontSize);
        result.advance += shaped.advance;
        result.rightToLeft = result.rightToLeft || shaped.rightToLeft;
        mergeMetrics(result.metrics, shaped.metrics, hasMetrics);
        result.glyphs.insert(result.glyphs.end(), shaped.glyphs.begin(), shaped.glyphs.end());
    }

    if (!hasMetrics)
        result.metrics = fallbackMetrics(fontSize);
    if (result.advance <= 0.0f)
        result.advance = static_cast<float>(utf8.size()) * 8.0f;

    return result;
#else
    return shapeWithFont(utf8, defaultUiFontFace(), fontSize);
#endif
}

MeasureResult TextShaper::measureSingleLine(const std::string& utf8, float fontSize) const
{
    GlyphRun run = shapeSingleRun(utf8, fontSize);
    return {{run.advance, run.metrics.lineHeight}, run.metrics.baseline, run.metrics.baseline};
}

} // namespace lute::ui
