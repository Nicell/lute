#include "lute/ui/Context.h"
#include "lute/ui/Platform.h"
#include "lute/ui/Style.h"
#include "lute/ui/Text.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#endif

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cliruntimefixture.h"
#include "doctest.h"

using namespace lute::ui;

#if defined(__APPLE__)
static double coreTextLineWidth(const std::string& utf8)
{
    CFStringRef string = CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(utf8.data()),
        static_cast<CFIndex>(utf8.size()),
        kCFStringEncodingUTF8,
        false
    );
    if (!string)
        return 0.0;

    CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, kDefaultUiFontSize, nullptr);
    if (!font)
    {
        CFRelease(string);
        return 0.0;
    }

    CFStringRef fontKey = kCTFontAttributeName;
    const void* keys[] = {fontKey};
    const void* values[] = {font};
    CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, nullptr, nullptr);
    CFAttributedStringRef attributed = CFAttributedStringCreate(kCFAllocatorDefault, string, attributes);
    CTLineRef line = attributed ? CTLineCreateWithAttributedString(attributed) : nullptr;
    double width = line ? CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr) : 0.0;

    if (line)
        CFRelease(line);
    if (attributed)
        CFRelease(attributed);
    if (attributes)
        CFRelease(attributes);
    CFRelease(font);
    CFRelease(string);
    return width;
}
#endif

TEST_CASE("ui_signal_invalidates_only_observed_text")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId column = context.createNode(WidgetKind::Column);
    NodeId observedText = context.createNode(WidgetKind::Text);
    NodeId staticText = context.createNode(WidgetKind::Text);

    context.setRoot(window);
    context.nodes().appendChild(window, column);
    context.nodes().appendChild(column, observedText);
    context.nodes().appendChild(column, staticText);
    context.nodes().setText(staticText, "Static");

    Signal<int> count(context.reactive(), 0);
    Effect effect(
        context.reactive(),
        [&]()
        {
            context.nodes().setText(observedText, "Count: " + std::to_string(count.get()));
        }
    );

    context.flush();
    CHECK(context.nodes().get(observedText)->dirty == DirtyBits::None);
    CHECK(context.nodes().get(staticText)->dirty == DirtyBits::None);

    count.set(1);

    CHECK(hasDirty(context.nodes().get(observedText)->dirty, DirtyBits::Text));
    CHECK(hasDirty(context.nodes().get(observedText)->dirty, DirtyBits::Layout));
    CHECK(context.nodes().get(staticText)->dirty == DirtyBits::None);
}

TEST_CASE("ui_column_layout_and_button_activation")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId column = context.createNode(WidgetKind::Column);
    NodeId text = context.createNode(WidgetKind::Text);
    NodeId button = context.createNode(WidgetKind::Button);

    context.setRoot(window);
    context.nodes().appendChild(window, column);
    context.nodes().appendChild(column, text);
    context.nodes().appendChild(column, button);
    context.nodes().setGap(column, 12.0f);
    context.nodes().setPadding(column, EdgeInsets::all(24.0f));
    context.nodes().setText(text, "Count: 0");
    context.nodes().setText(button, "Increment");

    int activations = 0;
    context.nodes().setOnActivate(
        button,
        [&]()
        {
            activations++;
        }
    );
    context.flush();

    const UiNode* textNode = context.nodes().get(text);
    const UiNode* buttonNode = context.nodes().get(button);

    CHECK(textNode->layout.frame.x == 24.0f);
    CHECK(textNode->layout.frame.y == 24.0f);
    CHECK(buttonNode->layout.frame.x == 24.0f);
    CHECK(buttonNode->layout.frame.y == textNode->layout.frame.y + textNode->layout.frame.height + 12.0f);
    CHECK(buttonNode->layout.frame.height == 32.0f);

    PointerEvent click;
    click.kind = PointerEventKind::Up;
    click.position = {30.0f, 70.0f};
    CHECK(context.dispatchPointer(click));
    CHECK(activations == 1);
    REQUIRE(context.focusedNode());
    CHECK(*context.focusedNode() == button);
}

TEST_CASE("ui_button_metrics_feed_layout_scene_input_and_semantics")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId button = context.createNode(WidgetKind::Button);

    context.setRoot(window);
    context.nodes().appendChild(window, button);
    context.nodes().setPadding(button, EdgeInsets{10.0f, 20.0f, 10.0f, 20.0f});
    context.nodes().setText(button, "Go");

    int activations = 0;
    context.nodes().setOnActivate(
        button,
        [&]()
        {
            activations++;
        }
    );

    context.flush();

    const UiNode* buttonNode = context.nodes().get(button);
    REQUIRE(buttonNode);
    REQUIRE(buttonNode->shapedText);

    ControlMetrics metrics = resolveButtonMetrics(*buttonNode);
    Vec2 labelSize{buttonNode->shapedText->advance, buttonNode->shapedText->metrics.lineHeight};
    CHECK(buttonNode->layout.measuredSize == controlPreferredSize(metrics, labelSize));
    REQUIRE(buttonNode->layout.firstBaseline);
    CHECK(*buttonNode->layout.firstBaseline == controlFirstBaseline(metrics, buttonNode->shapedText->metrics.baseline));

    REQUIRE(context.focus(button));
    context.flush();

    buttonNode = context.nodes().get(button);
    REQUIRE(buttonNode);
    metrics = resolveButtonMetrics(*buttonNode);

    const std::vector<DisplayItem>& items = context.currentScene().items();
    auto textItem = std::find_if(
        items.begin(),
        items.end(),
        [button](const DisplayItem& item)
        {
            return item.kind == DisplayItemKind::TextRun && item.node == button;
        }
    );
    REQUIRE(textItem != items.end());
    CHECK(textItem->origin == controlLabelOrigin(*buttonNode, metrics));

    auto focusRing = std::find_if(
        items.begin(),
        items.end(),
        [&](const DisplayItem& item)
        {
            return item.kind == DisplayItemKind::RoundedRect && item.node == button && item.rect == controlFocusRingBounds(*buttonNode, metrics);
        }
    );
    REQUIRE(focusRing != items.end());
    CHECK(focusRing->radius == controlFocusRingRadius(metrics));

    const std::vector<SemanticNode>& semanticNodes = context.currentSemantics().nodes();
    auto semantic = std::find_if(
        semanticNodes.begin(),
        semanticNodes.end(),
        [button](const SemanticNode& node)
        {
            return node.id == button;
        }
    );
    REQUIRE(semantic != semanticNodes.end());
    CHECK(semantic->bounds == controlSemanticBounds(*buttonNode, metrics));

    lute::ui::Rect hitBounds = controlHitBounds(*buttonNode, metrics);
    PointerEvent click;
    click.kind = PointerEventKind::Up;
    click.position = {hitBounds.x + hitBounds.width * 0.5f, hitBounds.y + hitBounds.height * 0.5f};
    CHECK(context.dispatchPointer(click));
    CHECK(activations == 1);
}

TEST_CASE("ui_focus_traversal_and_keyboard_activation")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId column = context.createNode(WidgetKind::Column);
    NodeId first = context.createNode(WidgetKind::Button);
    NodeId disabled = context.createNode(WidgetKind::Button);
    NodeId second = context.createNode(WidgetKind::Button);

    context.setRoot(window);
    context.nodes().appendChild(window, column);
    context.nodes().appendChild(column, first);
    context.nodes().appendChild(column, disabled);
    context.nodes().appendChild(column, second);
    context.nodes().setText(first, "First");
    context.nodes().setText(disabled, "Disabled");
    context.nodes().setText(second, "Second");
    context.nodes().setDisabled(disabled, true);

    int firstActivations = 0;
    int secondActivations = 0;
    context.nodes().setOnActivate(first, [&]() { firstActivations++; });
    context.nodes().setOnActivate(disabled, []() {});
    context.nodes().setOnActivate(second, [&]() { secondActivations++; });
    context.flush();

    KeyEvent tab;
    tab.kind = KeyEventKind::Down;
    tab.physical = PhysicalKey::Tab;
    tab.logical = LogicalKey::Tab;
    CHECK(context.dispatchKey(tab));
    REQUIRE(context.focusedNode());
    CHECK(*context.focusedNode() == first);

    KeyEvent enter;
    enter.kind = KeyEventKind::Down;
    enter.physical = PhysicalKey::Enter;
    enter.logical = LogicalKey::Enter;
    CHECK(context.dispatchKey(enter));
    CHECK(firstActivations == 1);
    CHECK(secondActivations == 0);

    CHECK(context.dispatchKey(tab));
    REQUIRE(context.focusedNode());
    CHECK(*context.focusedNode() == second);

    KeyEvent space;
    space.kind = KeyEventKind::Down;
    space.physical = PhysicalKey::Space;
    space.logical = LogicalKey::Space;
    CHECK(context.dispatchKey(space));
    CHECK(secondActivations == 0);

    space.kind = KeyEventKind::Up;
    CHECK(context.dispatchKey(space));
    CHECK(secondActivations == 1);

    tab.modifiers.shift = true;
    CHECK(context.dispatchKey(tab));
    REQUIRE(context.focusedNode());
    CHECK(*context.focusedNode() == first);
    context.flush();
    CHECK(context.currentSemantics().dump().find("focused") != std::string::npos);
}

TEST_CASE("ui_pointer_hover_pressed_and_capture_state")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId button = context.createNode(WidgetKind::Button);

    context.setRoot(window);
    context.nodes().appendChild(window, button);
    context.nodes().setPadding(window, EdgeInsets::all(24.0f));
    context.nodes().setText(button, "Increment");

    int activations = 0;
    context.nodes().setOnActivate(
        button,
        [&]()
        {
            activations++;
        }
    );
    context.flush();

    const UiNode* buttonNode = context.nodes().get(button);
    REQUIRE(buttonNode);
    lute::ui::Rect hitBounds = controlHitBounds(*buttonNode, resolveButtonMetrics(*buttonNode));
    Vec2 center{hitBounds.x + hitBounds.width * 0.5f, hitBounds.y + hitBounds.height * 0.5f};
    Vec2 outside{hitBounds.x + hitBounds.width + 20.0f, hitBounds.y + hitBounds.height + 20.0f};

    PointerEvent pointer;
    pointer.kind = PointerEventKind::Move;
    pointer.position = center;
    CHECK(context.dispatchPointer(pointer));
    REQUIRE(context.hoveredNode());
    CHECK(*context.hoveredNode() == button);
    CHECK(context.nodes().get(button)->hovered);

    context.flush();
    buttonNode = context.nodes().get(button);
    REQUIRE(buttonNode);
    ControlMetrics hoverMetrics = resolveButtonMetrics(*buttonNode);
    auto hoverFill = std::find_if(
        context.currentScene().items().begin(),
        context.currentScene().items().end(),
        [button, buttonNode, hoverMetrics](const DisplayItem& item)
        {
            return item.kind == DisplayItemKind::RoundedRect && item.node == button && item.rect == controlVisualBounds(*buttonNode) &&
                   item.fill.color == hoverMetrics.fillColor;
        }
    );
    REQUIRE(hoverFill != context.currentScene().items().end());

    pointer.kind = PointerEventKind::Down;
    CHECK(context.dispatchPointer(pointer));
    REQUIRE(context.focusedNode());
    CHECK(*context.focusedNode() == button);
    REQUIRE(context.capturedPointerNode());
    CHECK(*context.capturedPointerNode() == button);
    REQUIRE(context.pressedNode());
    CHECK(*context.pressedNode() == button);

    pointer.kind = PointerEventKind::Move;
    pointer.position = outside;
    CHECK(context.dispatchPointer(pointer));
    CHECK_FALSE(context.hoveredNode());
    CHECK_FALSE(context.pressedNode());
    REQUIRE(context.capturedPointerNode());
    CHECK(*context.capturedPointerNode() == button);

    pointer.kind = PointerEventKind::Up;
    CHECK(context.dispatchPointer(pointer));
    CHECK_FALSE(context.capturedPointerNode());
    CHECK_FALSE(context.pressedNode());
    CHECK(activations == 0);

    pointer.kind = PointerEventKind::Down;
    pointer.position = center;
    CHECK(context.dispatchPointer(pointer));
    pointer.kind = PointerEventKind::Move;
    pointer.position = outside;
    CHECK(context.dispatchPointer(pointer));
    pointer.position = center;
    CHECK(context.dispatchPointer(pointer));
    REQUIRE(context.pressedNode());
    CHECK(*context.pressedNode() == button);
    pointer.kind = PointerEventKind::Up;
    CHECK(context.dispatchPointer(pointer));
    CHECK(activations == 1);
    CHECK_FALSE(context.capturedPointerNode());
    CHECK_FALSE(context.pressedNode());
}

TEST_CASE("ui_text_input_and_ime_composition_route_to_focused_node")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId input = context.createNode(WidgetKind::Text);

    context.setRoot(window);
    context.nodes().appendChild(window, input);
    context.nodes().setText(input, "Input");

    std::string committed;
    std::vector<ImeCompositionEvent> compositionEvents;
    context.nodes().setOnTextInput(
        input,
        [&](const TextInputEvent& event)
        {
            committed += event.text;
        }
    );
    context.nodes().setOnImeComposition(
        input,
        [&](const ImeCompositionEvent& event)
        {
            compositionEvents.push_back(event);
        }
    );

    context.flush();
    REQUIRE(context.focus(input));

    KeyEvent space;
    space.kind = KeyEventKind::Down;
    space.physical = PhysicalKey::Space;
    space.logical = LogicalKey::Space;
    CHECK_FALSE(context.dispatchKey(space));

    ImeCompositionEvent start;
    start.kind = ImeCompositionEventKind::Start;
    start.text = "n";
    start.selectionStart = 0;
    start.selectionEnd = 1;
    CHECK(context.dispatchImeComposition(start));

    ImeCompositionEvent update;
    update.kind = ImeCompositionEventKind::Update;
    update.text = "ni";
    update.selectionStart = 0;
    update.selectionEnd = 2;
    CHECK(context.dispatchImeComposition(update));

    TextInputEvent text;
    text.text = "ni";
    CHECK(context.dispatchTextInput(text));

    ImeCompositionEvent end;
    end.kind = ImeCompositionEventKind::End;
    CHECK(context.dispatchImeComposition(end));

    CHECK(committed == "ni");
    REQUIRE(compositionEvents.size() == 3);
    CHECK(compositionEvents[0].kind == ImeCompositionEventKind::Start);
    CHECK(compositionEvents[0].text == "n");
    CHECK(compositionEvents[1].kind == ImeCompositionEventKind::Update);
    CHECK(compositionEvents[1].text == "ni");
    CHECK(compositionEvents[2].kind == ImeCompositionEventKind::End);

    REQUIRE(context.focus(input));
    context.nodes().setDisabled(input, true);
    CHECK_FALSE(context.dispatchTextInput(text));
}

TEST_CASE("ui_native_accessibility_actions_route_through_semantics")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId column = context.createNode(WidgetKind::Column);
    NodeId text = context.createNode(WidgetKind::Text);
    NodeId button = context.createNode(WidgetKind::Button);

    context.setRoot(window);
    context.nodes().appendChild(window, column);
    context.nodes().appendChild(column, text);
    context.nodes().appendChild(column, button);
    context.nodes().setText(text, "Count: 0");
    context.nodes().setText(button, "Increment");

    int activations = 0;
    context.nodes().setOnActivate(
        button,
        [&]()
        {
            activations++;
        }
    );
    context.flush();

    NativeAccessibilityBridge& bridge = nativeAccessibilityBridge();
    bridge.syncTree(context.currentSemantics());

    CHECK(bridge.performAction(context, button, SemanticAction::Focus));
    REQUIRE(context.focusedNode());
    CHECK(*context.focusedNode() == button);
    CHECK(context.currentSemantics().dump().find("focused") != std::string::npos);

    CHECK(bridge.performAction(context, button, SemanticAction::Activate));
    CHECK(activations == 1);

    CHECK_FALSE(bridge.performAction(context, text, SemanticAction::Activate));

    context.nodes().setDisabled(button, true);
    context.flush();
    bridge.syncTree(context.currentSemantics());
    CHECK_FALSE(bridge.performAction(context, button, SemanticAction::Activate));
    CHECK(activations == 1);
}

TEST_CASE("ui_flush_skips_clean_layout_scene_and_semantics")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId column = context.createNode(WidgetKind::Column);
    NodeId text = context.createNode(WidgetKind::Text);
    NodeId button = context.createNode(WidgetKind::Button);

    context.setRoot(window);
    context.nodes().appendChild(window, column);
    context.nodes().appendChild(column, text);
    context.nodes().appendChild(column, button);
    context.nodes().setGap(column, 12.0f);
    context.nodes().setPadding(column, EdgeInsets::all(24.0f));
    context.nodes().setText(text, "Count: 0");
    context.nodes().setText(button, "Increment");
    context.nodes().setOnActivate(button, []() {});

    context.flush();

    uint64_t windowLayout = context.nodes().get(window)->layout.generation;
    uint64_t columnLayout = context.nodes().get(column)->layout.generation;
    uint64_t textLayout = context.nodes().get(text)->layout.generation;
    uint64_t buttonLayout = context.nodes().get(button)->layout.generation;
    uint64_t sceneGeneration = context.currentScene().generation();
    uint64_t semanticGeneration = context.currentSemantics().generation();

    context.flush();

    CHECK(context.nodes().get(window)->layout.generation == windowLayout);
    CHECK(context.nodes().get(column)->layout.generation == columnLayout);
    CHECK(context.nodes().get(text)->layout.generation == textLayout);
    CHECK(context.nodes().get(button)->layout.generation == buttonLayout);
    CHECK(context.currentScene().generation() == sceneGeneration);
    CHECK(context.currentSemantics().generation() == semanticGeneration);
}

TEST_CASE("ui_flush_recomputes_only_signal_affected_layout_branch")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId column = context.createNode(WidgetKind::Column);
    NodeId observedText = context.createNode(WidgetKind::Text);
    NodeId staticText = context.createNode(WidgetKind::Text);
    NodeId button = context.createNode(WidgetKind::Button);

    context.setRoot(window);
    context.nodes().appendChild(window, column);
    context.nodes().appendChild(column, observedText);
    context.nodes().appendChild(column, staticText);
    context.nodes().appendChild(column, button);
    context.nodes().setGap(column, 12.0f);
    context.nodes().setPadding(column, EdgeInsets::all(24.0f));
    context.nodes().setText(staticText, "Static");
    context.nodes().setText(button, "Increment");
    context.nodes().setOnActivate(button, []() {});

    Signal<int> count(context.reactive(), 0);
    Effect effect(
        context.reactive(),
        [&]()
        {
            context.nodes().setText(observedText, "Count: " + std::to_string(count.get()));
        }
    );

    context.flush();

    uint64_t windowLayout = context.nodes().get(window)->layout.generation;
    uint64_t columnLayout = context.nodes().get(column)->layout.generation;
    uint64_t observedLayout = context.nodes().get(observedText)->layout.generation;
    uint64_t staticLayout = context.nodes().get(staticText)->layout.generation;
    uint64_t buttonLayout = context.nodes().get(button)->layout.generation;
    uint64_t sceneGeneration = context.currentScene().generation();
    uint64_t semanticGeneration = context.currentSemantics().generation();

    count.set(1);
    context.flush();

    CHECK(context.nodes().get(window)->layout.generation > windowLayout);
    CHECK(context.nodes().get(column)->layout.generation > columnLayout);
    CHECK(context.nodes().get(observedText)->layout.generation > observedLayout);
    CHECK(context.nodes().get(staticText)->layout.generation == staticLayout);
    CHECK(context.nodes().get(button)->layout.generation == buttonLayout);
    CHECK(context.currentScene().generation() == sceneGeneration + 1);
    CHECK(context.currentSemantics().generation() == semanticGeneration + 1);
    CHECK(context.nodes().get(observedText)->dirty == DirtyBits::None);
    CHECK(context.nodes().get(staticText)->dirty == DirtyBits::None);

    sceneGeneration = context.currentScene().generation();
    semanticGeneration = context.currentSemantics().generation();
    context.flush();
    CHECK(context.currentScene().generation() == sceneGeneration);
    CHECK(context.currentSemantics().generation() == semanticGeneration);
}

TEST_CASE("ui_state_change_updates_scene_and_semantics_without_relayout")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId column = context.createNode(WidgetKind::Column);
    NodeId button = context.createNode(WidgetKind::Button);

    context.setRoot(window);
    context.nodes().appendChild(window, column);
    context.nodes().appendChild(column, button);
    context.nodes().setPadding(column, EdgeInsets::all(24.0f));
    context.nodes().setText(button, "Increment");
    context.nodes().setOnActivate(button, []() {});
    context.flush();

    uint64_t windowLayout = context.nodes().get(window)->layout.generation;
    uint64_t columnLayout = context.nodes().get(column)->layout.generation;
    uint64_t buttonLayout = context.nodes().get(button)->layout.generation;
    uint64_t sceneGeneration = context.currentScene().generation();
    uint64_t semanticGeneration = context.currentSemantics().generation();

    context.nodes().setDisabled(button, true);
    context.flush();

    CHECK(context.nodes().get(window)->layout.generation == windowLayout);
    CHECK(context.nodes().get(column)->layout.generation == columnLayout);
    CHECK(context.nodes().get(button)->layout.generation == buttonLayout);
    CHECK(context.currentScene().generation() == sceneGeneration + 1);
    CHECK(context.currentSemantics().generation() == semanticGeneration + 1);
    CHECK(context.currentSemantics().dump().find("disabled") != std::string::npos);
}

TEST_CASE("ui_text_shapes_emoji_with_coretext_fallback")
{
    TextShaper shaper;
    GlyphRun run = shaper.shapeSingleRun(std::string("Hi ") + "\xf0\x9f\x99\x82");

    CHECK(run.advance > 0.0f);
    CHECK(std::any_of(
        run.glyphs.begin(),
        run.glyphs.end(),
        [](const ShapedGlyph& glyph)
        {
            return glyph.id != 0 && glyph.fontFace && glyph.fontFace->postScriptName().find("AppleColorEmoji") != std::string::npos;
        }
    ));

#if defined(__APPLE__)
    GlyphRun counter = shaper.shapeSingleRun(std::string("\xf0\x9f\xa7\xae") + " Count: 0");
    auto emojiGlyph = std::find_if(
        counter.glyphs.begin(),
        counter.glyphs.end(),
        [](const ShapedGlyph& glyph)
        {
            return glyph.id != 0 && glyph.fontFace && glyph.fontFace->postScriptName().find("AppleColorEmoji") != std::string::npos;
        }
    );

    REQUIRE(emojiGlyph != counter.glyphs.end());
    CHECK(emojiGlyph->xAdvance > 18.0f);
    GlyphMetrics emojiMetrics = emojiGlyph->fontFace->glyphMetrics(emojiGlyph->id);
    CHECK(emojiMetrics.available);
    CHECK(emojiMetrics.width > 16.0f);
    CHECK(emojiMetrics.yBearing + emojiMetrics.height < 0.0f);
    CHECK(counter.advance > 75.0f);
    double counterCoreText = coreTextLineWidth(std::string("\xf0\x9f\xa7\xae") + " Count: 0");
    CHECK(std::abs(counter.advance - counterCoreText) < 0.02);

    GlyphRun button = shaper.shapeSingleRun("Increment");
    double buttonCoreText = coreTextLineWidth("Increment");
    CHECK(std::abs(button.advance - buttonCoreText) < 0.02);
#endif
}

TEST_CASE("ui_scene_and_semantics_are_retained_outputs")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId column = context.createNode(WidgetKind::Column);
    NodeId text = context.createNode(WidgetKind::Text);
    NodeId button = context.createNode(WidgetKind::Button);

    context.setRoot(window);
    context.nodes().setTitle(window, "Lute UI");
    context.nodes().appendChild(window, column);
    context.nodes().appendChild(column, text);
    context.nodes().appendChild(column, button);
    context.nodes().setGap(column, 12.0f);
    context.nodes().setPadding(column, EdgeInsets::all(24.0f));
    context.nodes().setText(text, "Count: 0");
    context.nodes().setText(button, "Increment");
    context.nodes().setOnActivate(button, []() {});

    std::string scene = context.dumpScene();
    std::string semantics = context.dumpSemantics();
    RenderStats stats = context.render();

    CHECK(scene.find("TextRun") != std::string::npos);
    CHECK(scene.find("Count: 0") != std::string::npos);
    CHECK(scene.find("background=(245,245,242,255)") != std::string::npos);
    CHECK(scene.find("background=(38,101,214,255)") != std::string::npos);
    CHECK(semantics.find("Window#") != std::string::npos);
    CHECK(semantics.find("Button#") != std::string::npos);
    CHECK(semantics.find("label=\"Increment\"") != std::string::npos);
    CHECK(stats.backend == "Dawn");
    CHECK(stats.displayItemCount == 4);
}

TEST_CASE("ui_text_measurement_reuses_shaped_runs_for_scene_and_render")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId column = context.createNode(WidgetKind::Column);
    NodeId text = context.createNode(WidgetKind::Text);

    context.setRoot(window);
    context.nodes().appendChild(window, column);
    context.nodes().appendChild(column, text);
    context.nodes().setText(text, "Count: 0");

    context.flush();
    const UiNode* textNode = context.nodes().get(text);
    REQUIRE(textNode);
    REQUIRE(textNode->shapedText);

    const std::vector<DisplayItem>& items = context.currentScene().items();
    auto textItem = std::find_if(
        items.begin(),
        items.end(),
        [text](const DisplayItem& item)
        {
            return item.kind == DisplayItemKind::TextRun && item.node == text;
        }
    );
    REQUIRE(textItem != items.end());
    CHECK(textItem->glyphRun.get() == textNode->shapedText.get());

    context.render();
    std::optional<FrameProfile> latest = context.profiles().latest();
    REQUIRE(latest);
    CHECK(latest->counters.textRunsMeasured == 0);
}

TEST_CASE("ui_profile_records_frame_timings_and_counters")
{
    UiContext context;
    NodeId window = context.createNode(WidgetKind::Window);
    NodeId column = context.createNode(WidgetKind::Column);
    NodeId text = context.createNode(WidgetKind::Text);
    NodeId button = context.createNode(WidgetKind::Button);

    context.setRoot(window);
    context.nodes().appendChild(window, column);
    context.nodes().appendChild(column, text);
    context.nodes().appendChild(column, button);
    context.nodes().setGap(column, 12.0f);
    context.nodes().setPadding(column, EdgeInsets::all(24.0f));
    context.nodes().setText(text, "Count: 0");
    context.nodes().setText(button, "Increment");
    context.nodes().setOnActivate(button, []() {});

    RenderStats stats = context.render();
    std::optional<FrameProfile> latest = context.profiles().latest();
    REQUIRE(latest);

    CHECK(latest->label == "render");
    CHECK(latest->durationNs() > 0);
    CHECK(latest->phaseDurationNs(ProfilePhase::Layout) > 0);
    CHECK(latest->phaseDurationNs(ProfilePhase::Scene) > 0);
    CHECK(latest->counters.layoutNodesMeasured >= 4);
    CHECK(latest->counters.layoutNodesPlaced >= 4);
    CHECK(latest->counters.sceneItemsEmitted >= stats.displayItemCount);
    CHECK(latest->counters.displayItemsRendered == stats.displayItemCount);
    CHECK(latest->counters.renderCalls == 1);

    std::string dump = context.dumpProfile();
    CHECK(dump.find("Frame") != std::string::npos);
    CHECK(dump.find("layoutMeasured=") != std::string::npos);
    CHECK(dump.find("displayItems=") != std::string::npos);
}

TEST_CASE_FIXTURE(CliRuntimeFixture, "ui_luau_counter_updates_signal_bound_text")
{
    runCode(R"(
        local ui = require("@lute/ui")

        local count = ui.signal(0)
        local doubled = ui.computed(function()
            return count:get() * 2
        end)

        local root = ui.window {
            title = "Lute UI",

            ui.column {
                gap = 12,
                padding = 24,

                ui.text(function()
                    return "Count: " .. count:get()
                end),

                ui.button {
                    text = "Increment",
                    onPress = function()
                        count:set(count:get() + 1)
                    end,
                },
            },
        }

        ui.flush(root)
        report(ui.dump_scene(root))

        report(ui.click(root, 30, 70))
        ui.flush(root)

        report(ui.dump_scene(root))
        report(ui.dump_semantics(root))

        local stats = ui.render(root)
        report(doubled:get())
        report(stats.backend)
        report(stats.displayItemCount)
    )");

    REQUIRE(getReporter().getErrors().empty());
    REQUIRE(getReporter().getOutputs().size() == 7);

    CHECK(getReporter().getOutputs()[0].find("Count: 0") != std::string::npos);
    CHECK(getReporter().getOutputs()[1] == "true");
    CHECK(getReporter().getOutputs()[2].find("Count: 1") != std::string::npos);
    CHECK(getReporter().getOutputs()[3].find("label=\"Increment\"") != std::string::npos);
    CHECK(getReporter().getOutputs()[4] == "2");
    CHECK(getReporter().getOutputs()[5] == "Dawn");
    CHECK(getReporter().getOutputs()[6] == "4");
}

TEST_CASE_FIXTURE(CliRuntimeFixture, "ui_luau_dump_profile_reports_recent_frames")
{
    runCode(R"(
        local ui = require("@lute/ui")

        local root = ui.window {
            title = "Lute UI",

            ui.column {
                gap = 12,
                padding = 24,

                ui.text("Count: 0"),

                ui.button {
                    text = "Increment",
                    onPress = function() end,
                },
            },
        }

        ui.render(root)
        local profile = ui.dump_profile(root)
        report(profile:find("Frame") ~= nil)
        report(profile:find("displayItems=") ~= nil)
    )");

    REQUIRE(getReporter().getErrors().empty());
    REQUIRE(getReporter().getOutputs().size() == 2);

    CHECK(getReporter().getOutputs()[0] == "true");
    CHECK(getReporter().getOutputs()[1] == "true");
}

TEST_CASE_FIXTURE(CliRuntimeFixture, "ui_luau_keyboard_focus_activation")
{
    runCode(R"(
        local ui = require("@lute/ui")

        local count = ui.signal(0)
        local button = ui.button {
            text = "Increment",
            onPress = function()
                count:set(count:get() + 1)
            end,
        }

        local root = ui.window {
            title = "Lute UI",

            ui.column {
                gap = 12,
                padding = 24,

                ui.text(function()
                    return "Count: " .. count:get()
                end),

                button,
            },
        }

        ui.flush(root)

        report(ui.press_key(root, "Tab"))
        report(ui.focused(root) == button.id)
        report(ui.press_key(root, "Enter"))
        ui.flush(root)
        report(ui.dump_scene(root))
    )");

    REQUIRE(getReporter().getErrors().empty());
    REQUIRE(getReporter().getOutputs().size() == 4);

    CHECK(getReporter().getOutputs()[0] == "true");
    CHECK(getReporter().getOutputs()[1] == "true");
    CHECK(getReporter().getOutputs()[2] == "true");
    CHECK(getReporter().getOutputs()[3].find("Count: 1") != std::string::npos);
}
