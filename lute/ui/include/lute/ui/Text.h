#pragma once

#include "lute/ui/Layout.h"

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

class FontFace
{
public:
    FontFace();
    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;
    ~FontFace();

    bool available() const;
    const std::string& path() const;
    const std::string& postScriptName() const;
    uint32_t faceIndex() const;
    uint32_t unitsPerEm() const;
    FontMetrics metrics(float fontSize = kDefaultUiFontSize) const;

    hb_font_t* harfbuzzFont() const;

private:
    bool loadDefault();
    bool loadFromPath(std::string nextPath, std::string nextPostScriptName);

    std::string fontPath;
    std::string fontPostScriptName;
    uint32_t fontFaceIndex = 0;
    uint32_t fontUnitsPerEm = 1000;
    int fontAscender = 800;
    int fontDescender = -200;
    int fontLineGap = 0;
    bool loaded = false;

    hb_blob_t* blob = nullptr;
    hb_face_t* face = nullptr;
    hb_font_t* font = nullptr;
};

const FontFace& defaultUiFontFace();

struct ShapedGlyph
{
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
