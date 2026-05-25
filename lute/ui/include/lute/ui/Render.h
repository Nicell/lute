#pragma once

#include "lute/ui/Scene.h"

#include <string>

namespace lute::ui
{

struct RenderStats
{
    std::string backend;
    size_t displayItemCount = 0;
    uint64_t sceneGeneration = 0;
};

class DawnRenderer
{
public:
    RenderStats render(const Scene& scene);
    RenderStats renderToMetalLayer(const Scene& scene, void* metalLayer, uint32_t pixelWidth, uint32_t pixelHeight, float scale);
};

} // namespace lute::ui
