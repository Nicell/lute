#include "lute/ui/Render.h"

#include "lute/ui/Profile.h"

#if LUTE_UI_USE_DAWN
#include "DawnInternal.h"
#endif

namespace lute::ui
{

RenderStats DawnRenderer::render(const Scene& scene)
{
    UiProfiler::addRenderCall();
    UiProfiler::addDisplayItemsRendered(scene.items().size());
#if LUTE_UI_USE_DAWN
    if (dawn::backend().renderScene(scene))
        return {"Dawn", scene.items().size(), scene.generation()};
#endif

    return {"DawnUnavailable", scene.items().size(), scene.generation()};
}

RenderStats DawnRenderer::renderToMetalLayer(const Scene& scene, void* metalLayer, uint32_t pixelWidth, uint32_t pixelHeight, float scale)
{
    UiProfiler::addRenderCall();
    UiProfiler::addDisplayItemsRendered(scene.items().size());
#if LUTE_UI_USE_DAWN
    if (dawn::backend().renderToMetalLayer(scene, metalLayer, pixelWidth, pixelHeight, scale))
        return {"Dawn", scene.items().size(), scene.generation()};
#else
    (void)metalLayer;
    (void)pixelWidth;
    (void)pixelHeight;
    (void)scale;
#endif

    return {"DawnUnavailable", scene.items().size(), scene.generation()};
}

} // namespace lute::ui
