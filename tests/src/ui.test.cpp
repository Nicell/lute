#include "lute/ui/Context.h"
#include "lute/ui/Text.h"

#include <algorithm>
#include <string>

#include "cliruntimefixture.h"
#include "doctest.h"

using namespace lute::ui;

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
