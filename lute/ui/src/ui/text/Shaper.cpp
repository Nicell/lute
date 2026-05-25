#include "lute/ui/Text.h"

#if LUTE_UI_USE_HARFBUZZ
#include "hb.h"
#endif

#include <algorithm>
#include <limits>

namespace lute::ui
{

GlyphRun TextShaper::shapeSingleRun(const std::string& utf8) const
{
#if LUTE_UI_USE_HARFBUZZ
    int length = static_cast<int>(std::min<size_t>(utf8.size(), static_cast<size_t>(std::numeric_limits<int>::max())));
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, utf8.data(), length, 0, length);
    hb_buffer_guess_segment_properties(buffer);

    hb_font_t* font = hb_font_create(hb_face_get_empty());
    hb_shape(font, buffer, nullptr, 0);

    unsigned glyphCount = 0;
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyphCount);

    float advance = 0.0f;
    for (unsigned i = 0; i < glyphCount; i++)
        advance += static_cast<float>(positions[i].x_advance) / 64.0f;

    bool rtl = HB_DIRECTION_IS_BACKWARD(hb_buffer_get_direction(buffer));

    hb_font_destroy(font);
    hb_buffer_destroy(buffer);

    if (advance <= 0.0f)
        advance = static_cast<float>(utf8.size()) * 8.0f;

    return {utf8, advance, rtl};
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

    return {utf8, static_cast<float>(utf8.size()) * 8.0f, rtl};
#endif
}

MeasureResult TextShaper::measureSingleLine(const std::string& utf8) const
{
    GlyphRun run = shapeSingleRun(utf8);
    return {{run.advance, 20.0f}, 15.0f, 15.0f};
}

} // namespace lute::ui
