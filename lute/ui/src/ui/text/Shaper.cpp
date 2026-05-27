#include "lute/ui/Text.h"

#include "lute/ui/Platform.h"
#include "lute/ui/Profile.h"

#if LUTE_UI_USE_HARFBUZZ
#include "hb.h"
#include "hb-ot.h"
#endif

#if LUTE_UI_USE_SIMDUTF
#include "simdutf.h"
#endif

#if LUTE_UI_USE_LIBGRAPHEME
extern "C" {
#include "grapheme.h"
}
#endif

#if LUTE_UI_USE_SHEENBIDI
#include <SheenBidi/SheenBidi.h>
#endif

#if defined(__APPLE__) && LUTE_UI_USE_HARFBUZZ
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lute::ui
{

namespace
{

using ResolvedFont = PlatformFontDescriptor;

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

static bool faceCoversText(hb_face_t* face, std::string_view utf8)
{
    if (!face || utf8.empty())
        return false;

    hb_font_t* probeFont = hb_font_create(face);
    hb_ot_font_set_funcs(probeFont);
    unsigned int upem = hb_face_get_upem(face);
    if (upem == 0)
        upem = 1000;
    hb_font_set_scale(probeFont, static_cast<int>(upem), static_cast<int>(upem));

    hb_buffer_t* buffer = hb_buffer_create();
    int length = static_cast<int>(std::min<size_t>(utf8.size(), static_cast<size_t>(std::numeric_limits<int>::max())));
    hb_buffer_add_utf8(buffer, utf8.data(), length, 0, length);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(probeFont, buffer, nullptr, 0);

    unsigned int glyphCount = 0;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
    bool covers = glyphCount > 0;
    for (unsigned int i = 0; i < glyphCount; i++)
    {
        if (infos[i].codepoint == 0)
        {
            covers = false;
            break;
        }
    }

    hb_buffer_destroy(buffer);
    hb_font_destroy(probeFont);
    return covers;
}

static uint32_t findFaceIndexByCoverage(hb_blob_t* blob, std::string_view utf8)
{
    if (!blob || utf8.empty())
        return 0;

    unsigned int faceCount = hb_face_count(blob);
    for (unsigned int i = 0; i < faceCount; i++)
    {
        hb_face_t* candidate = hb_face_create(blob, i);
        bool covers = faceCoversText(candidate, utf8);
        hb_face_destroy(candidate);

        if (covers)
            return i;
    }

    return 0;
}

static uint32_t findFaceIndex(hb_blob_t* blob, const std::string& postScriptName, std::string_view probeText)
{
    if (!blob || postScriptName.empty())
        return findFaceIndexByCoverage(blob, probeText);

    unsigned int faceCount = hb_face_count(blob);
    for (unsigned int i = 0; i < faceCount; i++)
    {
        hb_face_t* candidate = hb_face_create(blob, i);
        std::string candidateName = facePostScriptName(candidate);
        hb_face_destroy(candidate);

        if (candidateName == postScriptName)
            return i;
    }

    return findFaceIndexByCoverage(blob, probeText);
}

static uint16_t readU16(const char* data, unsigned int length, unsigned int offset)
{
    if (offset + 2 > length)
        return 0;

    return (static_cast<uint16_t>(static_cast<unsigned char>(data[offset])) << 8) | static_cast<uint16_t>(static_cast<unsigned char>(data[offset + 1]));
}

static int16_t readS16(const char* data, unsigned int length, unsigned int offset)
{
    return static_cast<int16_t>(readU16(data, length, offset));
}

static uint32_t readU32(const char* data, unsigned int length, unsigned int offset)
{
    if (offset + 4 > length)
        return 0;

    return (static_cast<uint32_t>(static_cast<unsigned char>(data[offset])) << 24) |
        (static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 16) |
        (static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 8) |
        static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 3]));
}

static int32_t readS32(const char* data, unsigned int length, unsigned int offset)
{
    uint32_t value = readU32(data, length, offset);
    if (value <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
        return static_cast<int32_t>(value);

    return -static_cast<int32_t>((~value) + 1u);
}

static float fixed16Dot16ToFloat(int32_t value)
{
    return static_cast<float>(value) / 65536.0f;
}

struct TrackingTable
{
    std::vector<float> sizes;
    std::vector<float> values;
};

static TrackingTable readTrackingTable(hb_face_t* face)
{
    TrackingTable result;
    if (!face)
        return result;

    hb_blob_t* blob = hb_face_reference_table(face, HB_TAG('t', 'r', 'a', 'k'));
    if (!blob)
        return result;

    unsigned int length = 0;
    const char* data = hb_blob_get_data(blob, &length);
    if (!data || length < 12)
    {
        hb_blob_destroy(blob);
        return result;
    }

    uint16_t format = readU16(data, length, 4);
    uint16_t horizontalOffset = readU16(data, length, 6);
    if (format != 0 || horizontalOffset == 0 || horizontalOffset + 8 > length)
    {
        hb_blob_destroy(blob);
        return result;
    }

    unsigned int trackData = horizontalOffset;
    uint16_t trackCount = readU16(data, length, trackData);
    uint16_t sizeCount = readU16(data, length, trackData + 2);
    uint32_t sizeTableOffset = readU32(data, length, trackData + 4);
    if (trackCount == 0 || sizeCount == 0 || trackData + 8u + static_cast<unsigned int>(trackCount) * 8u > length ||
        sizeTableOffset + static_cast<unsigned int>(sizeCount) * 4u > length)
    {
        hb_blob_destroy(blob);
        return result;
    }

    unsigned int normalTrackIndex = 0;
    float closestTrack = std::numeric_limits<float>::max();
    for (uint16_t i = 0; i < trackCount; i++)
    {
        unsigned int entryOffset = trackData + 8u + static_cast<unsigned int>(i) * 8u;
        float track = fixed16Dot16ToFloat(readS32(data, length, entryOffset));
        float distance = std::abs(track);
        if (distance < closestTrack)
        {
            closestTrack = distance;
            normalTrackIndex = i;
        }
    }

    unsigned int normalEntry = trackData + 8u + normalTrackIndex * 8u;
    uint16_t valueOffset = readU16(data, length, normalEntry + 6);
    if (valueOffset + static_cast<unsigned int>(sizeCount) * 2u > length)
    {
        hb_blob_destroy(blob);
        return result;
    }

    result.sizes.reserve(sizeCount);
    result.values.reserve(sizeCount);
    for (uint16_t i = 0; i < sizeCount; i++)
    {
        result.sizes.push_back(fixed16Dot16ToFloat(readS32(data, length, sizeTableOffset + static_cast<unsigned int>(i) * 4u)));
        result.values.push_back(static_cast<float>(readS16(data, length, valueOffset + static_cast<unsigned int>(i) * 2u)));
    }

    hb_blob_destroy(blob);
    return result;
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

struct DirectionRun
{
    size_t start = 0;
    size_t end = 0;
    bool rightToLeft = false;
};

struct ScriptRun
{
    size_t start = 0;
    size_t end = 0;
    uint32_t scriptTag = 0;
};

struct SanitizedText
{
    std::string text;
    bool validUtf8 = true;
};

struct WrappedRanges
{
    std::vector<std::pair<size_t, size_t>> ranges;
    TextLine reusableLine;
    bool hasReusableLine = false;
};

static bool decodeUtf8CodepointAt(std::string_view text, size_t offset, uint32_t& codepoint, size_t& next)
{
    if (offset >= text.size())
        return false;

    unsigned char first = static_cast<unsigned char>(text[offset]);
    size_t remaining = text.size() - offset;
    if (first < 0x80)
    {
        codepoint = first;
        next = offset + 1;
        return true;
    }

    auto continuation = [&](size_t index) {
        return index < text.size() && (static_cast<unsigned char>(text[index]) & 0xc0) == 0x80;
    };

    if (first >= 0xc2 && first <= 0xdf && remaining >= 2 && continuation(offset + 1))
    {
        unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        codepoint = ((first & 0x1f) << 6) | (b1 & 0x3f);
        next = offset + 2;
        return true;
    }

    if (first >= 0xe0 && first <= 0xef && remaining >= 3 && continuation(offset + 1) && continuation(offset + 2))
    {
        unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
        if ((first == 0xe0 && b1 < 0xa0) || (first == 0xed && b1 >= 0xa0))
            return false;
        codepoint = ((first & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f);
        next = offset + 3;
        return true;
    }

    if (first >= 0xf0 && first <= 0xf4 && remaining >= 4 && continuation(offset + 1) && continuation(offset + 2) && continuation(offset + 3))
    {
        unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
        unsigned char b3 = static_cast<unsigned char>(text[offset + 3]);
        if ((first == 0xf0 && b1 < 0x90) || (first == 0xf4 && b1 >= 0x90))
            return false;
        codepoint = ((first & 0x07) << 18) | ((b1 & 0x3f) << 12) | ((b2 & 0x3f) << 6) | (b3 & 0x3f);
        next = offset + 4;
        return true;
    }

    return false;
}

static void appendUtf8(std::string& output, uint32_t codepoint)
{
    if (codepoint <= 0x7f)
    {
        output.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7ff)
    {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
    else if (codepoint <= 0xffff)
    {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
    else
    {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

static SanitizedText sanitizeUtf8(const std::string& text)
{
    ProfileZone zone(ProfilePhase::TextUtfValidate);
    SanitizedText result;
#if LUTE_UI_USE_SIMDUTF
    if (simdutf::validate_utf8(text.data(), text.size()))
    {
        result.text = text;
        return result;
    }
#else
    bool valid = true;
    size_t probe = 0;
    while (probe < text.size())
    {
        uint32_t codepoint = 0;
        size_t next = probe;
        if (!decodeUtf8CodepointAt(text, probe, codepoint, next))
        {
            valid = false;
            break;
        }
        probe = next;
    }
    if (valid)
    {
        result.text = text;
        return result;
    }
#endif

    result.validUtf8 = false;
    result.text.reserve(text.size());
    size_t offset = 0;
    while (offset < text.size())
    {
        uint32_t codepoint = 0;
        size_t next = offset;
        if (decodeUtf8CodepointAt(text, offset, codepoint, next))
        {
            result.text.append(text.data() + offset, next - offset);
            offset = next;
        }
        else
        {
            appendUtf8(result.text, 0xfffd);
            offset++;
        }
    }
    return result;
}

#if !LUTE_UI_USE_LIBGRAPHEME
struct CodepointSpan
{
    uint32_t codepoint = 0;
    size_t start = 0;
    size_t end = 0;
};

static uint32_t decodeUtf8Codepoint(std::string_view text, size_t& offset)
{
    uint32_t codepoint = 0xfffd;
    size_t next = offset + 1;
    if (decodeUtf8CodepointAt(text, offset, codepoint, next))
    {
        offset = next;
        return codepoint;
    }

    offset = std::min(offset + 1, text.size());
    return 0xfffd;
}

static std::vector<CodepointSpan> codepointSpans(std::string_view text)
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

static std::vector<TextCluster> textClusters(std::string_view text)
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
#endif

static std::vector<TextCluster> graphemeClusters(std::string_view text)
{
    ProfileZone zone(ProfilePhase::TextGrapheme);
    if (text.empty())
        return {};

#if LUTE_UI_USE_LIBGRAPHEME
    std::vector<TextCluster> result;
    size_t offset = 0;
    while (offset < text.size())
    {
        uint32_t first = 0xfffd;
        size_t next = offset;
        (void)decodeUtf8CodepointAt(text, offset, first, next);

        size_t length = grapheme_next_character_break_utf8(text.data() + offset, text.size() - offset);
        if (length == 0)
            length = std::max<size_t>(1, next - offset);
        length = std::min(length, text.size() - offset);
        result.push_back({offset, offset + length, first});
        offset += length;
    }
    return result;
#else
    return textClusters(text);
#endif
}

static std::vector<size_t> lineBreakOffsets(std::string_view text)
{
    ProfileZone zone(ProfilePhase::TextLineBreak);
    std::vector<size_t> offsets;
    if (text.empty())
        return offsets;

#if LUTE_UI_USE_LIBGRAPHEME
    size_t offset = 0;
    while (offset < text.size())
    {
        size_t length = grapheme_next_line_break_utf8(text.data() + offset, text.size() - offset);
        if (length == 0)
            break;
        offset += std::min(length, text.size() - offset);
        offsets.push_back(offset);
    }
#else
    for (const TextCluster& cluster : textClusters(text))
    {
        if (cluster.firstCodepoint == ' ' || cluster.firstCodepoint == '\n' || cluster.firstCodepoint == '\r')
            offsets.push_back(cluster.end);
    }
#endif
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    return offsets;
}

static bool isBreakOpportunity(const std::vector<size_t>& offsets, size_t offset)
{
    return std::binary_search(offsets.begin(), offsets.end(), offset);
}

static bool isLineSeparatorCluster(std::string_view text, const TextCluster& cluster)
{
    if (cluster.start >= text.size())
        return false;
    char c = text[cluster.start];
    return c == '\n' || c == '\r';
}

static size_t trimTrailingLineWhitespace(std::string_view text, size_t start, size_t end)
{
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t'))
        end--;
    return end;
}

static size_t trimLeadingLineWhitespace(std::string_view text, size_t start, size_t end)
{
    while (start < end && (text[start] == ' ' || text[start] == '\t'))
        start++;
    return start;
}

static std::vector<size_t> graphemeBoundaryOffsets(std::string_view text)
{
    std::vector<size_t> offsets{0};
    for (const TextCluster& cluster : graphemeClusters(text))
        offsets.push_back(cluster.end);
    if (offsets.back() != text.size())
        offsets.push_back(text.size());
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    return offsets;
}

static bool isGraphemeBoundary(const std::vector<size_t>& boundaries, size_t offset)
{
    return std::binary_search(boundaries.begin(), boundaries.end(), offset);
}

static std::vector<DirectionRun> bidiRuns(std::string_view text)
{
    ProfileZone zone(ProfilePhase::TextBidi);
    std::vector<DirectionRun> runs;
    if (text.empty())
        return runs;

#if LUTE_UI_USE_SHEENBIDI
    SBCodepointSequence sequence{SBStringEncodingUTF8, text.data(), static_cast<SBUInteger>(text.size())};
    SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
    if (algorithm)
    {
        SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, 0, static_cast<SBUInteger>(text.size()), SBLevelDefaultLTR);
        if (paragraph)
        {
            SBLineRef line = SBParagraphCreateLine(paragraph, 0, SBParagraphGetLength(paragraph));
            if (line)
            {
                const SBRun* sheenRuns = SBLineGetRunsPtr(line);
                SBUInteger count = SBLineGetRunCount(line);
                runs.reserve(count);
                for (SBUInteger i = 0; i < count; i++)
                {
                    size_t start = std::min<size_t>(sheenRuns[i].offset, text.size());
                    size_t end = std::min<size_t>(start + sheenRuns[i].length, text.size());
                    if (start < end)
                        runs.push_back({start, end, (sheenRuns[i].level & 1u) != 0});
                }
                SBLineRelease(line);
            }
            SBParagraphRelease(paragraph);
        }
        SBAlgorithmRelease(algorithm);
    }
#endif

    if (runs.empty())
        runs.push_back({0, text.size(), false});
    return runs;
}

static uint32_t scriptTagFromSheenScript(uint32_t script)
{
#if LUTE_UI_USE_SHEENBIDI
    if (script == SBScriptNil || script == SBScriptZINH || script == SBScriptZYYY || script == SBScriptZZZZ)
        return 0;
    return SBScriptGetUnicodeTag(static_cast<SBScript>(script));
#else
    (void)script;
    return 0;
#endif
}

static std::vector<ScriptRun> scriptRuns(std::string_view text)
{
    ProfileZone zone(ProfilePhase::TextScript);
    std::vector<ScriptRun> runs;
    if (text.empty())
        return runs;

#if LUTE_UI_USE_SHEENBIDI
    SBScriptLocatorRef locator = SBScriptLocatorCreate();
    if (locator)
    {
        SBCodepointSequence sequence{SBStringEncodingUTF8, text.data(), static_cast<SBUInteger>(text.size())};
        SBScriptLocatorLoadCodepoints(locator, &sequence);
        while (SBScriptLocatorMoveNext(locator))
        {
            const SBScriptAgent* agent = SBScriptLocatorGetAgent(locator);
            if (!agent)
                continue;

            size_t start = std::min<size_t>(agent->offset, text.size());
            size_t end = std::min<size_t>(start + agent->length, text.size());
            if (start < end)
                runs.push_back({start, end, scriptTagFromSheenScript(agent->script)});
        }
        SBScriptLocatorRelease(locator);
    }
#endif

    if (runs.empty())
        runs.push_back({0, text.size(), 0});
    return runs;
}

static const ScriptRun* findScriptRun(const std::vector<ScriptRun>& runs, size_t offset)
{
    for (const ScriptRun& run : runs)
    {
        if (offset >= run.start && offset < run.end)
            return &run;
    }
    return runs.empty() ? nullptr : &runs.front();
}

static const FontRun* findFontRun(const std::vector<FontRun>& runs, size_t offset)
{
    for (const FontRun& run : runs)
    {
        if (offset >= run.start && offset < run.end)
            return &run;
    }
    return runs.empty() ? nullptr : &runs.front();
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
    if (resolved.postScriptName.empty() && !resolved.probeText.empty())
        key += "#" + resolved.probeText;
    auto found = faces.find(key);
    if (found != faces.end())
        return *found->second;

    auto face = std::make_unique<FontFace>(
        resolved.path,
        resolved.postScriptName,
        resolved.probeText,
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

static bool isAsciiCluster(std::string_view text)
{
    if (text.empty())
        return false;

    for (unsigned char byte : text)
    {
        if (byte >= 0x80)
            return false;
    }

    return true;
}

static std::unordered_map<std::string, const FontFace*>& clusterFontFaceCache()
{
    static std::unordered_map<std::string, const FontFace*> cache;
    return cache;
}

static const FontFace& fontFaceForCluster(std::string_view clusterText)
{
    const FontFace& defaultFace = defaultUiFontFace();
#if LUTE_UI_USE_HARFBUZZ
    auto& cache = clusterFontFaceCache();
    std::string key(clusterText);
    auto found = cache.find(key);
    if (found != cache.end())
        return *found->second;

    ResolvedFont resolved;
    const FontFace* face = &defaultFace;
    bool defaultCovers = isAsciiCluster(clusterText);
    if (!defaultCovers)
    {
        ProfileZone zone(ProfilePhase::TextFontCoverage);
        defaultCovers = faceCoversText(defaultFace.harfbuzzFace(), clusterText);
    }

    if (!defaultCovers)
    {
        ProfileZone zone(ProfilePhase::TextNativeFallback);
        if (nativeTextServices().resolveFallbackUIFont(clusterText, resolved))
            face = &fontFaceForResolvedFont(resolved);
    }

    cache.emplace(std::move(key), face);
    return *face;
#else
    (void)clusterText;
    return defaultFace;
#endif
}

static bool fallbackFaceCoversCluster(const FontFace* face, std::string_view clusterText)
{
#if LUTE_UI_USE_HARFBUZZ
    if (!face || face == &defaultUiFontFace() || clusterText.empty() || isAsciiCluster(clusterText))
        return false;

    ProfileZone zone(ProfilePhase::TextFontCoverage);
    return faceCoversText(face->harfbuzzFace(), clusterText);
#else
    (void)face;
    (void)clusterText;
    return false;
#endif
}

static uint64_t fallbackCacheKey(std::string_view text, const std::vector<ScriptRun>& scripts, size_t start)
{
    const ScriptRun* script = findScriptRun(scripts, start);
    if (script && script->scriptTag != 0)
        return (1ull << 63) | static_cast<uint64_t>(script->scriptTag);

    uint32_t codepoint = 0;
    size_t next = start;
    if (decodeUtf8CodepointAt(text, start, codepoint, next))
        return static_cast<uint64_t>(codepoint >> 8);

    return 0;
}

static std::unordered_map<uint64_t, const FontFace*>& fallbackRunFaceCache()
{
    static std::unordered_map<uint64_t, const FontFace*> cache;
    return cache;
}

static bool cachedFallbackFaceCovers(const FontFace* face, std::string_view text)
{
#if LUTE_UI_USE_HARFBUZZ
    if (!face || face == &defaultUiFontFace() || text.empty())
        return false;

    ProfileZone zone(ProfilePhase::TextFontCoverage);
    return faceCoversText(face->harfbuzzFace(), text);
#else
    (void)face;
    (void)text;
    return false;
#endif
}

static void appendFontRun(std::vector<FontRun>& runs, size_t start, size_t end, const FontFace* face)
{
    if (!face || start >= end)
        return;

    if (runs.empty() || runs.back().fontFace != face || runs.back().end != start)
        runs.push_back({start, end, face});
    else
        runs.back().end = end;
}

struct PendingFallbackRange
{
    NativeTextServices::FallbackFontRange range;
    uint64_t cacheKey = 0;
};

struct ResolvedFallbackRange
{
    size_t start = 0;
    size_t end = 0;
    const FontFace* face = nullptr;
};

static std::vector<FontRun> fallbackFontRuns(std::string_view utf8)
{
    ProfileZone zone(ProfilePhase::TextFontFallback);
    std::vector<FontRun> runs;
    if (utf8.empty())
        return runs;

#if LUTE_UI_USE_HARFBUZZ
    const FontFace& defaultFace = defaultUiFontFace();
    if (isAsciiCluster(utf8))
    {
        runs.push_back({0, utf8.size(), &defaultFace});
        return runs;
    }

    std::vector<TextCluster> clusters = graphemeClusters(utf8);
    std::vector<ScriptRun> scripts = scriptRuns(utf8);
    std::vector<PendingFallbackRange> fallbackRanges;
    std::optional<uint64_t> activeFallbackKey;
    for (const TextCluster& cluster : clusters)
    {
        std::string_view clusterText(utf8.data() + cluster.start, cluster.end - cluster.start);
        if (isAsciiCluster(clusterText))
        {
            activeFallbackKey.reset();
            continue;
        }

        bool defaultCovers = false;
        {
            ProfileZone coverageZone(ProfilePhase::TextFontCoverage);
            defaultCovers = faceCoversText(defaultFace.harfbuzzFace(), clusterText);
        }
        if (defaultCovers)
        {
            activeFallbackKey.reset();
            continue;
        }

        uint64_t cacheKey = fallbackCacheKey(utf8, scripts, cluster.start);
        if (!fallbackRanges.empty() && fallbackRanges.back().range.end == cluster.start && activeFallbackKey && *activeFallbackKey == cacheKey)
        {
            fallbackRanges.back().range.end = cluster.end;
        }
        else
        {
            fallbackRanges.push_back({{cluster.start, cluster.end}, cacheKey});
            activeFallbackKey = cacheKey;
        }
    }

    std::vector<ResolvedFallbackRange> resolvedRanges;
    std::vector<NativeTextServices::FallbackFontRange> nativeRequests;
    auto& fallbackCache = fallbackRunFaceCache();
    for (const PendingFallbackRange& pending : fallbackRanges)
    {
        auto cached = fallbackCache.find(pending.cacheKey);
        std::string_view rangeText(utf8.data() + pending.range.start, pending.range.end - pending.range.start);
        if (cached != fallbackCache.end() && cachedFallbackFaceCovers(cached->second, rangeText))
        {
            resolvedRanges.push_back({pending.range.start, pending.range.end, cached->second});
        }
        else
        {
            nativeRequests.push_back(pending.range);
        }
    }

    std::vector<NativeTextServices::FallbackFontRun> nativeRuns;
    bool resolvedNativeRuns = false;
    if (!nativeRequests.empty())
    {
        ProfileZone nativeZone(ProfilePhase::TextNativeFallback);
        resolvedNativeRuns = nativeTextServices().resolveFallbackUIFontRuns(utf8, nativeRequests, nativeRuns);
    }

    if (resolvedNativeRuns)
    {
        for (const NativeTextServices::FallbackFontRun& nativeRun : nativeRuns)
        {
            size_t start = std::min(nativeRun.start, utf8.size());
            size_t end = std::min(nativeRun.end, utf8.size());
            if (start >= end)
                continue;

            const FontFace& face = fontFaceForResolvedFont(nativeRun.font);
            fallbackCache[fallbackCacheKey(utf8, scripts, start)] = &face;
            resolvedRanges.push_back({start, end, &face});
        }
    }

    if (!resolvedRanges.empty())
    {
        std::sort(resolvedRanges.begin(), resolvedRanges.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.start != rhs.start)
                return lhs.start < rhs.start;
            return lhs.end < rhs.end;
        });

        size_t cursor = 0;
        for (const ResolvedFallbackRange& resolved : resolvedRanges)
        {
            size_t start = std::min(resolved.start, utf8.size());
            size_t end = std::min(resolved.end, utf8.size());
            if (start >= end)
                continue;

            if (start > cursor)
                appendFontRun(runs, cursor, start, &defaultFace);

            appendFontRun(runs, start, end, resolved.face);
            cursor = std::max(cursor, end);
        }

        if (cursor < utf8.size())
            appendFontRun(runs, cursor, utf8.size(), &defaultFace);
        if (!runs.empty())
            return runs;
    }

    const FontFace* previousFace = nullptr;
    for (const TextCluster& cluster : clusters)
    {
        std::string_view clusterText(utf8.data() + cluster.start, cluster.end - cluster.start);
        const FontFace* face = previousFace && fallbackFaceCoversCluster(previousFace, clusterText) ? previousFace : &fontFaceForCluster(clusterText);
        previousFace = face;

        appendFontRun(runs, cluster.start, cluster.end, face);
    }
#else
    runs.push_back({0, utf8.size(), &defaultUiFontFace()});
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

static GlyphRun shapeWithFont(
    std::string_view utf8,
    const FontFace& fontFace,
    float fontSize,
    bool rightToLeft = false,
    uint32_t scriptTag = 0,
    size_t sourceStart = 0
)
{
#if LUTE_UI_USE_HARFBUZZ
    if (!fontFace.available() || !fontFace.harfbuzzFont())
        return {std::string(utf8), {}, static_cast<float>(utf8.size()) * 8.0f, fontSize, fallbackMetrics(fontSize), rightToLeft};

    int length = static_cast<int>(std::min<size_t>(utf8.size(), static_cast<size_t>(std::numeric_limits<int>::max())));
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, utf8.data(), length, 0, length);
    hb_buffer_set_direction(buffer, rightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    if (scriptTag != 0)
        hb_buffer_set_script(buffer, hb_script_from_iso15924_tag(scriptTag));
    hb_buffer_set_language(buffer, hb_language_get_default());
    if (scriptTag == 0)
        hb_buffer_guess_segment_properties(buffer);
    {
        ProfileZone zone(ProfilePhase::TextHbShape);
        hb_shape(fontFace.harfbuzzFont(), buffer, nullptr, 0);
    }

    unsigned glyphCount = 0;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyphCount);

    float unitToPixel = fontSize / static_cast<float>(fontFace.unitsPerEm());
    float tracking = fontFace.tracking(fontSize);
    std::vector<ShapedGlyph> glyphs;
    glyphs.reserve(glyphCount);
    std::vector<size_t> clusterOffsets;
    clusterOffsets.reserve(glyphCount + 1);

    float advance = 0.0f;
    for (unsigned i = 0; i < glyphCount; i++)
    {
        ShapedGlyph glyph;
        glyph.fontFace = &fontFace;
        glyph.id = infos[i].codepoint;
        glyph.cluster = infos[i].cluster;
        size_t localCluster = std::min<size_t>(infos[i].cluster, utf8.size());
        glyph.sourceStart = sourceStart + localCluster;
        float harfbuzzXAdvance = static_cast<float>(positions[i].x_advance) * unitToPixel;
        glyph.xAdvance = harfbuzzXAdvance;
        glyph.yAdvance = static_cast<float>(positions[i].y_advance) * unitToPixel;
        glyph.xOffset = static_cast<float>(positions[i].x_offset) * unitToPixel;
        glyph.yOffset = static_cast<float>(positions[i].y_offset) * unitToPixel;
        clusterOffsets.push_back(localCluster);
        bool usedPlatformAdvance = false;
        if (fontFace.prefersPlatformGlyphMetrics() && std::abs(harfbuzzXAdvance) > 0.0001f)
        {
            GlyphMetrics metrics = fontFace.glyphMetrics(glyph.id, fontSize);
            if (metrics.available && metrics.xAdvance > 0.0f)
            {
                glyph.xAdvance = metrics.xAdvance;
                glyph.yAdvance = metrics.yAdvance;
                usedPlatformAdvance = true;
            }
        }
        if (!usedPlatformAdvance)
            glyph.xAdvance += tracking;
        advance += glyph.xAdvance;
        glyphs.push_back(glyph);
    }

    clusterOffsets.push_back(utf8.size());
    std::sort(clusterOffsets.begin(), clusterOffsets.end());
    clusterOffsets.erase(std::unique(clusterOffsets.begin(), clusterOffsets.end()), clusterOffsets.end());
    for (ShapedGlyph& glyph : glyphs)
    {
        size_t localCluster = glyph.sourceStart >= sourceStart ? glyph.sourceStart - sourceStart : 0;
        auto next = std::upper_bound(clusterOffsets.begin(), clusterOffsets.end(), localCluster);
        glyph.sourceEnd = sourceStart + (next == clusterOffsets.end() ? utf8.size() : *next);
    }

    bool rtl = HB_DIRECTION_IS_BACKWARD(hb_buffer_get_direction(buffer));

    hb_buffer_destroy(buffer);

    if (advance <= 0.0f)
        advance = static_cast<float>(utf8.size()) * 8.0f;

    GlyphRun result;
    result.text.assign(utf8.data(), utf8.size());
    result.glyphs = std::move(glyphs);
    result.advance = advance;
    result.fontSize = fontSize;
    result.metrics = fontFace.metrics(fontSize);
    result.rightToLeft = rtl;
    return result;
#else
    (void)fontFace;
    (void)scriptTag;
    (void)sourceStart;
    return {std::string(utf8), {}, static_cast<float>(utf8.size()) * 8.0f, fontSize, fallbackMetrics(fontSize), rightToLeft};
#endif
}

static TextLine shapeLineRange(std::string_view text, size_t start, size_t end, float fontSize)
{
    TextLine line;
    line.sourceStart = start;
    line.sourceEnd = end;

    const FontFace& defaultFace = defaultUiFontFace();
    FontMetrics metrics = defaultFace.available() ? defaultFace.metrics(fontSize) : fallbackMetrics(fontSize);
    float penX = 0.0f;

    if (start >= end || start >= text.size())
    {
        line.metrics = metrics;
        line.lineHeight = metrics.lineHeight;
        line.baseline = metrics.baseline;
        return line;
    }

    end = std::min(end, text.size());
    std::string_view lineText(text.data() + start, end - start);
    std::vector<DirectionRun> directions = bidiRuns(lineText);
    std::vector<ScriptRun> scripts = scriptRuns(lineText);
    std::vector<FontRun> fonts = fallbackFontRuns(lineText);
    std::vector<size_t> graphemeBoundaries = graphemeBoundaryOffsets(lineText);

    for (const DirectionRun& direction : directions)
    {
        std::vector<size_t> splitPoints{direction.start, direction.end};
        for (const ScriptRun& script : scripts)
        {
            if (script.start < direction.end && script.end > direction.start)
            {
                size_t splitStart = std::max(script.start, direction.start);
                size_t splitEnd = std::min(script.end, direction.end);
                if (isGraphemeBoundary(graphemeBoundaries, splitStart))
                    splitPoints.push_back(splitStart);
                if (isGraphemeBoundary(graphemeBoundaries, splitEnd))
                    splitPoints.push_back(splitEnd);
            }
        }
        for (const FontRun& font : fonts)
        {
            if (font.start < direction.end && font.end > direction.start)
            {
                size_t splitStart = std::max(font.start, direction.start);
                size_t splitEnd = std::min(font.end, direction.end);
                if (isGraphemeBoundary(graphemeBoundaries, splitStart))
                    splitPoints.push_back(splitStart);
                if (isGraphemeBoundary(graphemeBoundaries, splitEnd))
                    splitPoints.push_back(splitEnd);
            }
        }
        std::sort(splitPoints.begin(), splitPoints.end());
        splitPoints.erase(std::unique(splitPoints.begin(), splitPoints.end()), splitPoints.end());

        std::vector<std::pair<size_t, size_t>> segments;
        segments.reserve(splitPoints.size() > 0 ? splitPoints.size() - 1 : 0);
        for (size_t i = 1; i < splitPoints.size(); i++)
            segments.push_back({splitPoints[i - 1], splitPoints[i]});
        if (direction.rightToLeft)
            std::reverse(segments.begin(), segments.end());

        for (const auto& segment : segments)
        {
            size_t segmentStart = segment.first;
            size_t segmentEnd = segment.second;
            if (segmentStart >= segmentEnd)
                continue;

            const ScriptRun* script = findScriptRun(scripts, segmentStart);
            const FontRun* font = findFontRun(fonts, segmentStart);
            const FontFace* face = font && font->fontFace ? font->fontFace : &defaultFace;
            uint32_t scriptTag = script ? script->scriptTag : 0;
            GlyphRun shaped = shapeWithFont(
                std::string_view(lineText.data() + segmentStart, segmentEnd - segmentStart),
                *face,
                fontSize,
                direction.rightToLeft,
                scriptTag,
                start + segmentStart
            );

            auto shared = std::make_shared<GlyphRun>(std::move(shaped));
            line.rightToLeft = line.rightToLeft || shared->rightToLeft;
            line.runs.push_back({shared, penX, start + segmentStart, start + segmentEnd});
            penX += shared->advance;
        }
    }

    line.advance = penX;
    line.metrics = metrics;
    line.lineHeight = metrics.lineHeight;
    line.baseline = metrics.baseline;
    return line;
}

static GlyphRun flattenLineRun(std::string_view sourceText, const TextLine& line, float fontSize)
{
    GlyphRun result;
    result.text.assign(sourceText.data(), sourceText.size());
    result.fontSize = fontSize;
    result.metrics = line.metrics;
    result.rightToLeft = line.rightToLeft;

    for (const TextRunFragment& fragment : line.runs)
    {
        if (!fragment.glyphRun)
            continue;
        result.advance += fragment.glyphRun->advance;
        result.glyphs.insert(result.glyphs.end(), fragment.glyphRun->glyphs.begin(), fragment.glyphRun->glyphs.end());
    }

    if (result.metrics.lineHeight <= 0.0f)
        result.metrics = fallbackMetrics(fontSize);
    return result;
}

static std::vector<float> clusterAdvancesFromLine(const TextLine& line, const std::vector<TextCluster>& clusters)
{
    std::vector<float> advances(clusters.size(), 0.0f);
    if (clusters.empty())
        return advances;

    std::vector<size_t> clusterStarts;
    clusterStarts.reserve(clusters.size());
    for (const TextCluster& cluster : clusters)
        clusterStarts.push_back(cluster.start);

    for (const TextRunFragment& fragment : line.runs)
    {
        if (!fragment.glyphRun)
            continue;

        for (const ShapedGlyph& glyph : fragment.glyphRun->glyphs)
        {
            auto next = std::upper_bound(clusterStarts.begin(), clusterStarts.end(), glyph.sourceStart);
            if (next == clusterStarts.begin())
                continue;

            size_t index = static_cast<size_t>(std::distance(clusterStarts.begin(), next) - 1);
            if (index < clusters.size() && glyph.sourceStart < clusters[index].end)
                advances[index] += glyph.xAdvance;
        }
    }

    return advances;
}

static WrappedRanges wrappedLineRanges(std::string_view text, float fontSize, float maxWidth)
{
    ProfileZone zone(ProfilePhase::TextWrap);
    WrappedRanges result;
    std::vector<TextCluster> clusters = graphemeClusters(text);

    if (clusters.empty())
    {
        result.ranges.push_back({0, 0});
        return result;
    }

    std::vector<float> clusterAdvances;
    std::vector<size_t> clusterStarts;
    std::vector<size_t> breakOffsets;
    if (maxWidth > 0.0f)
    {
        TextLine unwrapped = shapeLineRange(text, 0, text.size(), fontSize);
        bool hasHardBreak = false;
        for (const TextCluster& cluster : clusters)
        {
            if (isLineSeparatorCluster(text, cluster))
            {
                hasHardBreak = true;
                break;
            }
        }

        if (!hasHardBreak && unwrapped.advance <= maxWidth)
        {
            result.ranges.push_back({0, trimTrailingLineWhitespace(text, 0, text.size())});
            if (result.ranges.back().second == text.size())
            {
                result.reusableLine = std::move(unwrapped);
                result.hasReusableLine = true;
            }
            return result;
        }

        clusterAdvances = clusterAdvancesFromLine(unwrapped, clusters);
        clusterStarts.reserve(clusters.size());
        for (const TextCluster& cluster : clusters)
            clusterStarts.push_back(cluster.start);
        breakOffsets = lineBreakOffsets(text);
    }

    size_t lineStart = 0;
    size_t lastBreak = 0;
    float lineAdvance = 0.0f;
    size_t i = 0;
    while (i < clusters.size())
    {
        const TextCluster& cluster = clusters[i];
        if (isLineSeparatorCluster(text, cluster))
        {
            size_t lineEnd = trimTrailingLineWhitespace(text, lineStart, cluster.start);
            result.ranges.push_back({lineStart, lineEnd});
            lineStart = cluster.end;
            lastBreak = lineStart;
            lineAdvance = 0.0f;
            i++;
            continue;
        }

        float clusterAdvance = i < clusterAdvances.size() ? clusterAdvances[i] : 0.0f;
        if (maxWidth > 0.0f && cluster.start > lineStart && lineAdvance + clusterAdvance > maxWidth)
        {
            size_t breakAt = lastBreak > lineStart && lastBreak <= cluster.start ? lastBreak : cluster.start;
            size_t lineEnd = trimTrailingLineWhitespace(text, lineStart, breakAt);
            result.ranges.push_back({lineStart, lineEnd});
            lineStart = trimLeadingLineWhitespace(text, breakAt, text.size());
            lastBreak = lineStart;
            lineAdvance = 0.0f;

            auto next = std::lower_bound(clusterStarts.begin(), clusterStarts.end(), lineStart);
            i = static_cast<size_t>(std::distance(clusterStarts.begin(), next));
            continue;
        }

        lineAdvance += clusterAdvance;
        if (maxWidth > 0.0f && isBreakOpportunity(breakOffsets, cluster.end))
            lastBreak = cluster.end;
        i++;
    }

    result.ranges.push_back({lineStart, trimTrailingLineWhitespace(text, lineStart, text.size())});
    return result;
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
    std::string probeText,
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
        std::move(probeText),
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

float FontFace::tracking(float fontSize) const
{
    if (trackingSizes.empty() || trackingValues.empty() || fontUnitsPerEm == 0)
        return 0.0f;

    float value = trackingValues.front();
    if (fontSize <= trackingSizes.front())
    {
        value = trackingValues.front();
    }
    else if (fontSize >= trackingSizes.back())
    {
        value = trackingValues.back();
    }
    else
    {
        for (size_t i = 1; i < trackingSizes.size(); i++)
        {
            if (fontSize > trackingSizes[i])
                continue;

            float previousSize = trackingSizes[i - 1];
            float nextSize = trackingSizes[i];
            float t = nextSize == previousSize ? 0.0f : (fontSize - previousSize) / (nextSize - previousSize);
            value = trackingValues[i - 1] + (trackingValues[i] - trackingValues[i - 1]) * t;
            break;
        }
    }

    return value * fontSize / static_cast<float>(fontUnitsPerEm);
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
    nativeTextServices().resolveDefaultUIFont(resolved);

    if (resolved.path.empty() && !resolveFallbackFont(resolved))
        return false;

    return loadFromPath(
        std::move(resolved.path),
        std::move(resolved.postScriptName),
        std::move(resolved.probeText),
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
    std::string nextProbeText,
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

    uint32_t nextFaceIndex = findFaceIndex(nextBlob, nextPostScriptName, nextProbeText);
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
    TrackingTable tracking = readTrackingTable(nextFace);
    trackingSizes = std::move(tracking.sizes);
    trackingValues = std::move(tracking.values);
    platformMetricsAvailable = platformFont && platformAscenderRatio > 0.0f;
    preferPlatformGlyphMetrics = platformFont && (hb_ot_color_has_png(nextFace) || hb_ot_color_has_svg(nextFace));
    return true;
#else
    (void)nextPath;
    (void)nextPostScriptName;
    (void)nextProbeText;
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
    SanitizedText sanitized = sanitizeUtf8(utf8);
    TextLine line = shapeLineRange(sanitized.text, 0, sanitized.text.size(), fontSize);
    GlyphRun result = flattenLineRun(sanitized.text, line, fontSize);
    if (result.advance <= 0.0f && !utf8.empty())
        result.advance = static_cast<float>(utf8.size()) * 8.0f;
    return result;
}

TextLayout TextShaper::layoutParagraph(const std::string& utf8, float fontSize, float maxWidth) const
{
    SanitizedText sanitized = sanitizeUtf8(utf8);

    TextLayout layout;
    layout.text = sanitized.text;
    layout.fontSize = fontSize;
    layout.maxWidth = maxWidth;
    layout.validUtf8 = sanitized.validUtf8;

    WrappedRanges wrapped = wrappedLineRanges(sanitized.text, fontSize, maxWidth);
    float y = 0.0f;
    float width = 0.0f;
    bool hasMetrics = false;
    for (size_t i = 0; i < wrapped.ranges.size(); i++)
    {
        const auto& range = wrapped.ranges[i];
        TextLine line = wrapped.hasReusableLine && i == 0
            ? std::move(wrapped.reusableLine)
            : shapeLineRange(sanitized.text, range.first, range.second, fontSize);
        line.baseline += y;
        y += line.lineHeight;
        width = std::max(width, line.advance);
        mergeMetrics(layout.metrics, line.metrics, hasMetrics);
        layout.wrapped = layout.wrapped || range.second < sanitized.text.size();
        layout.lines.push_back(std::move(line));
    }

    if (layout.lines.empty())
    {
        TextLine line = shapeLineRange(sanitized.text, 0, 0, fontSize);
        width = line.advance;
        y = line.lineHeight;
        layout.metrics = line.metrics;
        layout.lines.push_back(std::move(line));
        hasMetrics = true;
    }

    if (!hasMetrics)
        layout.metrics = fallbackMetrics(fontSize);

    layout.size = {width, y};
    return layout;
}

MeasureResult TextShaper::measureSingleLine(const std::string& utf8, float fontSize) const
{
    GlyphRun run = shapeSingleRun(utf8, fontSize);
    return {{run.advance, run.metrics.lineHeight}, run.metrics.baseline, run.metrics.baseline};
}

} // namespace lute::ui
