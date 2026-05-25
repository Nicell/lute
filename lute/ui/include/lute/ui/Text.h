#pragma once

#include "lute/ui/Layout.h"

#include <string>
#include <vector>

namespace lute::ui
{

using ParagraphId = uint64_t;

struct GlyphRun
{
    std::string text;
    float advance = 0.0f;
    bool rightToLeft = false;
};

class TextShaper
{
public:
    GlyphRun shapeSingleRun(const std::string& utf8) const;
    MeasureResult measureSingleLine(const std::string& utf8) const;
};

} // namespace lute::ui
