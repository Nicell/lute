#include "lute/ui/Context.h"

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
