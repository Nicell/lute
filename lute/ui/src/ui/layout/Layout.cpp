#include "lute/ui/Layout.h"
#include "lute/ui/Profile.h"
#include "lute/ui/Style.h"
#include "lute/ui/Text.h"

#include <algorithm>
#include <memory>
#include <sstream>

namespace lute::ui
{

static float clampFloat(float value, float minValue, float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

static Vec2 clampSize(Vec2 size, Constraints constraints)
{
    return {clampFloat(size.x, constraints.minWidth, constraints.maxWidth), clampFloat(size.y, constraints.minHeight, constraints.maxHeight)};
}

static Constraints childConstraints(Constraints constraints, EdgeInsets padding)
{
    return {
        0.0f,
        std::max(0.0f, constraints.maxWidth - padding.horizontal()),
        0.0f,
        std::max(0.0f, constraints.maxHeight - padding.vertical()),
    };
}

static MeasureResult measureGlyphRun(const GlyphRun& run)
{
    return {{run.advance, run.metrics.lineHeight}, run.metrics.baseline, run.metrics.baseline};
}

static MeasureResult measureTextLayout(const TextLayout& layout)
{
    std::optional<float> firstBaseline;
    std::optional<float> lastBaseline;
    if (!layout.lines.empty())
    {
        firstBaseline = layout.lines.front().baseline;
        lastBaseline = layout.lines.back().baseline;
    }
    return {layout.size, firstBaseline, lastBaseline};
}

static std::shared_ptr<const GlyphRun> flattenLayoutRun(const TextLayout& layout)
{
    auto run = std::make_shared<GlyphRun>();
    run->text = layout.text;
    run->fontSize = layout.fontSize;
    run->metrics = layout.metrics;

    if (layout.lines.size() == 1)
    {
        for (const TextRunFragment& fragment : layout.lines.front().runs)
        {
            if (!fragment.glyphRun)
                continue;
            run->advance += fragment.glyphRun->advance;
            run->rightToLeft = run->rightToLeft || fragment.glyphRun->rightToLeft;
            run->glyphs.insert(run->glyphs.end(), fragment.glyphRun->glyphs.begin(), fragment.glyphRun->glyphs.end());
        }
    }
    else
    {
        for (const TextLine& line : layout.lines)
            run->advance = std::max(run->advance, line.advance);
    }

    return run;
}

static std::shared_ptr<const TextLayout> layoutText(UiNode& node, float maxWidth)
{
    if (node.textLayout && node.textLayout->text == node.text && node.textLayout->fontSize == kDefaultUiFontSize && node.textLayout->maxWidth == maxWidth)
        return node.textLayout;

    UiProfiler::addTextRunMeasured();
    ProfileZone zone(ProfilePhase::Text);
    TextShaper shaper;
    node.textLayout = std::make_shared<TextLayout>(shaper.layoutParagraph(node.text, kDefaultUiFontSize, maxWidth));
    if (node.textLayout->lines.size() == 1 && node.textLayout->lines.front().runs.size() == 1)
        node.shapedText = node.textLayout->lines.front().runs.front().glyphRun;
    else
        node.shapedText = flattenLayoutRun(*node.textLayout);
    return node.textLayout;
}

static std::shared_ptr<const GlyphRun> shapeSingleLineText(UiNode& node)
{
    if (node.shapedText && node.shapedText->text == node.text && node.shapedText->fontSize == kDefaultUiFontSize)
        return node.shapedText;

    UiProfiler::addTextRunMeasured();
    ProfileZone zone(ProfilePhase::Text);
    TextShaper shaper;
    node.shapedText = std::make_shared<GlyphRun>(shaper.shapeSingleRun(node.text));
    node.textLayout.reset();
    return node.shapedText;
}

static MeasureResult textMeasure(UiNode& node, float maxWidth)
{
    return measureTextLayout(*layoutText(node, maxWidth));
}

void LayoutEngine::layout(NodeTree& tree, NodeId root, Vec2 viewport)
{
    if (root == kInvalidNodeId || !tree.get(root))
        return;

    Constraints constraints = Constraints::loose(viewport);
    measure(tree, root, constraints);
    place(tree, root, {0.0f, 0.0f, viewport.x, viewport.y});
}

MeasureResult LayoutEngine::measure(NodeTree& tree, NodeId id, Constraints constraints)
{
    UiNode* node = tree.get(id);
    if (!node)
        return {{0.0f, 0.0f}, std::nullopt, std::nullopt};

    if (!hasDirty(node->dirty, DirtyBits::Layout) && !hasDirty(node->dirty, DirtyBits::Text) && node->layout.measuredSize.x != 0.0f &&
        node->layout.measuredSize.y != 0.0f)
    {
        return {node->layout.measuredSize, node->layout.firstBaseline, node->layout.lastBaseline};
    }

    return measureNode(tree, *node, constraints);
}

void LayoutEngine::place(NodeTree& tree, NodeId id, Rect rect)
{
    UiNode* node = tree.get(id);
    if (!node)
        return;

    placeNode(tree, *node, rect, false);
}

MeasureResult LayoutEngine::measureNode(NodeTree& tree, UiNode& node, Constraints constraints)
{
    MeasureResult result{{0.0f, 0.0f}, std::nullopt, std::nullopt};
    UiProfiler::addLayoutNodeMeasured();

    switch (node.kind)
    {
    case WidgetKind::Window:
    case WidgetKind::Box:
    case WidgetKind::Canvas:
    {
        Vec2 childSize{0.0f, 0.0f};
        if (!node.children.empty())
        {
            MeasureResult child = measure(tree, node.children.front(), childConstraints(constraints, node.padding));
            childSize = child.size;
        }

        result.size = clampSize({childSize.x + node.padding.horizontal(), childSize.y + node.padding.vertical()}, constraints);
        break;
    }
    case WidgetKind::Column:
    {
        Constraints inner = childConstraints(constraints, node.padding);
        float width = 0.0f;
        float height = 0.0f;

        for (size_t i = 0; i < node.children.size(); i++)
        {
            MeasureResult child = measure(tree, node.children[i], inner);
            width = std::max(width, child.size.x);
            height += child.size.y;
            if (i + 1 < node.children.size())
                height += node.gap;
        }

        result.size = clampSize({width + node.padding.horizontal(), height + node.padding.vertical()}, constraints);
        break;
    }
    case WidgetKind::Row:
    {
        Constraints inner = childConstraints(constraints, node.padding);
        float width = 0.0f;
        float height = 0.0f;

        for (size_t i = 0; i < node.children.size(); i++)
        {
            MeasureResult child = measure(tree, node.children[i], inner);
            width += child.size.x;
            height = std::max(height, child.size.y);
            if (i + 1 < node.children.size())
                width += node.gap;
        }

        result.size = clampSize({width + node.padding.horizontal(), height + node.padding.vertical()}, constraints);
        break;
    }
    case WidgetKind::Text:
    {
        MeasureResult measured = textMeasure(node, constraints.maxWidth);
        result.size = clampSize(measured.size, constraints);
        result.firstBaseline = measured.firstBaseline;
        result.lastBaseline = measured.lastBaseline;
        break;
    }
    case WidgetKind::Button:
    {
        ControlMetrics metrics = resolveButtonMetrics(node);
        MeasureResult label = measureGlyphRun(*shapeSingleLineText(node));
        result.size = clampSize(controlPreferredSize(metrics, label.size), constraints);
        result.firstBaseline = controlFirstBaseline(metrics, label.firstBaseline);
        result.lastBaseline = result.firstBaseline;
        break;
    }
    }

    node.layout.measuredSize = result.size;
    node.layout.firstBaseline = result.firstBaseline;
    node.layout.lastBaseline = result.lastBaseline;
    node.layout.generation++;
    tree.clearDirty(node.id, DirtyBits::Layout | DirtyBits::Text);

    return result;
}

void LayoutEngine::placeNode(NodeTree& tree, UiNode& node, Rect rect, bool force)
{
    bool frameChanged = node.layout.frame != rect;
    bool needsPlacement = force || frameChanged || hasDirty(node.dirty, DirtyBits::Layout) || hasDirty(node.dirty, DirtyBits::HitTest);
    if (!needsPlacement)
        return;

    node.layout.frame = rect;
    UiProfiler::addLayoutNodePlaced();
    bool forceChildren = force || frameChanged;

    switch (node.kind)
    {
    case WidgetKind::Window:
    case WidgetKind::Box:
    case WidgetKind::Canvas:
    {
        if (!node.children.empty())
        {
            UiNode* child = tree.get(node.children.front());
            if (child)
            {
                placeNode(
                    tree,
                    *child,
                    {rect.x + node.padding.left, rect.y + node.padding.top, child->layout.measuredSize.x, child->layout.measuredSize.y},
                    forceChildren
                );
            }
        }
        break;
    }
    case WidgetKind::Column:
    {
        float cursorY = rect.y + node.padding.top;
        for (NodeId childId : node.children)
        {
            UiNode* child = tree.get(childId);
            if (!child)
                continue;

            placeNode(tree, *child, {rect.x + node.padding.left, cursorY, child->layout.measuredSize.x, child->layout.measuredSize.y}, forceChildren);
            cursorY += child->layout.measuredSize.y + node.gap;
        }
        break;
    }
    case WidgetKind::Row:
    {
        float cursorX = rect.x + node.padding.left;
        for (NodeId childId : node.children)
        {
            UiNode* child = tree.get(childId);
            if (!child)
                continue;

            placeNode(tree, *child, {cursorX, rect.y + node.padding.top, child->layout.measuredSize.x, child->layout.measuredSize.y}, forceChildren);
            cursorX += child->layout.measuredSize.x + node.gap;
        }
        break;
    }
    case WidgetKind::Text:
    case WidgetKind::Button:
        break;
    }

    tree.clearDirty(node.id, DirtyBits::HitTest);
}

static void dumpLayoutNode(const NodeTree& tree, NodeId id, int depth, std::ostringstream& out)
{
    const UiNode* node = tree.get(id);
    if (!node)
        return;

    for (int i = 0; i < depth; i++)
        out << "  ";

    const Rect& frame = node->layout.frame;
    out << toString(node->kind) << "#" << node->id << " frame=(" << frame.x << "," << frame.y << "," << frame.width << "," << frame.height << ")"
        << " measured=(" << node->layout.measuredSize.x << "," << node->layout.measuredSize.y << ")"
        << " gen=" << node->layout.generation << "\n";

    for (NodeId child : node->children)
        dumpLayoutNode(tree, child, depth + 1, out);
}

std::string LayoutEngine::dump(const NodeTree& tree, NodeId root) const
{
    std::ostringstream out;
    dumpLayoutNode(tree, root, 0, out);
    return out.str();
}

} // namespace lute::ui
