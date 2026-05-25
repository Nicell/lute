#pragma once

#include "lute/ui/Layout.h"

#include <memory>
#include <string>
#include <vector>

struct hb_blob_t;
struct hb_face_t;
struct hb_font_t;

namespace lute::ui
{

using ParagraphId = uint64_t;

inline constexpr float kDefaultUiFontSize = 14.0f;

struct FontMetrics
{
    float ascender = 11.0f;
    float descender = 4.0f;
    float lineGap = 0.0f;
    float lineHeight = 20.0f;
    float baseline = 15.0f;
};

struct GlyphMetrics
{
    bool available = false;
    float xAdvance = 0.0f;
    float yAdvance = 0.0f;
    float xBearing = 0.0f;
    float yBearing = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct FontVariation
{
    uint32_t tag = 0;
    float value = 0.0f;
};

class FontFace
{
public:
    FontFace();
    FontFace(std::string path, std::string postScriptName);
    FontFace(
        std::string path,
        std::string postScriptName,
        std::shared_ptr<const void> platformFont,
        float platformAscenderRatio,
        float platformDescenderRatio,
        float platformLineGapRatio,
        std::vector<FontVariation> platformVariations
    );
    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;
    ~FontFace();

    bool available() const;
    const std::string& path() const;
    const std::string& postScriptName() const;
    uint32_t faceIndex() const;
    uint32_t unitsPerEm() const;
    FontMetrics metrics(float fontSize = kDefaultUiFontSize) const;
    GlyphMetrics glyphMetrics(uint32_t glyph, float fontSize = kDefaultUiFontSize) const;
    bool prefersPlatformGlyphMetrics() const;

    hb_face_t* harfbuzzFace() const;
    hb_font_t* harfbuzzFont() const;

private:
    bool loadDefault();
    bool loadFromPath(
        std::string nextPath,
        std::string nextPostScriptName,
        std::shared_ptr<const void> nextPlatformFont = {},
        float nextPlatformAscenderRatio = 0.0f,
        float nextPlatformDescenderRatio = 0.0f,
        float nextPlatformLineGapRatio = 0.0f,
        std::vector<FontVariation> nextPlatformVariations = {}
    );

    std::string fontPath;
    std::string fontPostScriptName;
    std::shared_ptr<const void> platformFont;
    uint32_t fontFaceIndex = 0;
    uint32_t fontUnitsPerEm = 1000;
    int fontAscender = 800;
    int fontDescender = -200;
    int fontLineGap = 0;
    float platformAscenderRatio = 0.0f;
    float platformDescenderRatio = 0.0f;
    float platformLineGapRatio = 0.0f;
    bool platformMetricsAvailable = false;
    bool preferPlatformGlyphMetrics = false;
    bool loaded = false;

    hb_blob_t* blob = nullptr;
    hb_face_t* face = nullptr;
    hb_font_t* font = nullptr;
};

const FontFace& defaultUiFontFace();

struct ShapedGlyph
{
    const FontFace* fontFace = nullptr;
    uint32_t id = 0;
    float xAdvance = 0.0f;
    float yAdvance = 0.0f;
    float xOffset = 0.0f;
    float yOffset = 0.0f;
};

struct GlyphRun
{
    std::string text;
    std::vector<ShapedGlyph> glyphs;
    float advance = 0.0f;
    float fontSize = kDefaultUiFontSize;
    FontMetrics metrics;
    bool rightToLeft = false;
};

class TextShaper
{
public:
    GlyphRun shapeSingleRun(const std::string& utf8, float fontSize = kDefaultUiFontSize) const;
    MeasureResult measureSingleLine(const std::string& utf8, float fontSize = kDefaultUiFontSize) const;
};

} // namespace lute::ui
