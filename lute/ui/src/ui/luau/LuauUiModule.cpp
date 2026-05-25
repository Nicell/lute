#include "lute/ref.h"
#include "lute/ui.h"
#include "lute/ui/Context.h"
#include "lute/ui/Platform.h"
#include "lute/userdatas.h"

#include "lua.h"
#include "lualib.h"

#include <cctype>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

namespace lute::ui
{

struct LuaValue
{
    std::variant<std::monostate, bool, double, std::string> value;

    bool operator==(const LuaValue& rhs) const
    {
        return value == rhs.value;
    }
};

struct LuaUiState
{
    std::shared_ptr<UiContext> context = std::make_shared<UiContext>();
};

struct LuaElement
{
    std::shared_ptr<UiContext> context;
    NodeId id = kInvalidNodeId;
};

struct LuaSignal
{
    std::shared_ptr<UiContext> context;
    std::shared_ptr<Signal<LuaValue>> signal;
};

static LuaUiState* checkState(lua_State* L, int index)
{
    auto* state = static_cast<LuaUiState*>(lua_touserdatatagged(L, index, kUiContextTag));
    if (!state)
        luaL_errorL(L, "invalid ui context");
    return state;
}

static LuaUiState* closureState(lua_State* L)
{
    return checkState(L, lua_upvalueindex(1));
}

static LuaElement* checkElement(lua_State* L, int index)
{
    auto* element = static_cast<LuaElement*>(lua_touserdatatagged(L, index, kUiElementTag));
    if (!element)
        luaL_errorL(L, "expected ui element");
    return element;
}

static LuaSignal* checkSignal(lua_State* L, int index)
{
    auto* signal = static_cast<LuaSignal*>(lua_touserdatatagged(L, index, kUiSignalTag));
    if (!signal)
        luaL_errorL(L, "expected ui signal");
    return signal;
}

static LuaValue readLuaValue(lua_State* L, int index)
{
    switch (lua_type(L, index))
    {
    case LUA_TNIL:
        return {};
    case LUA_TBOOLEAN:
        return {{static_cast<bool>(lua_toboolean(L, index))}};
    case LUA_TNUMBER:
        return {{lua_tonumber(L, index)}};
    case LUA_TSTRING:
    {
        size_t length = 0;
        const char* string = lua_tolstring(L, index, &length);
        return {{std::string(string, length)}};
    }
    default:
        luaL_errorL(L, "ui values must be nil, boolean, number, or string");
        return {};
    }
}

static void pushLuaValue(lua_State* L, const LuaValue& value)
{
    if (std::holds_alternative<std::monostate>(value.value))
    {
        lua_pushnil(L);
        return;
    }

    if (const bool* boolean = std::get_if<bool>(&value.value))
    {
        lua_pushboolean(L, *boolean);
        return;
    }

    if (const double* number = std::get_if<double>(&value.value))
    {
        lua_pushnumber(L, *number);
        return;
    }

    const std::string& string = std::get<std::string>(value.value);
    lua_pushlstring(L, string.data(), string.size());
}

static std::string valueToText(lua_State* L, int index)
{
    size_t length = 0;
    const char* string = luaL_tolstring(L, index, &length);
    std::string result(string, length);
    lua_pop(L, 1);
    return result;
}

static EdgeInsets readPadding(lua_State* L, int index)
{
    if (lua_isnumber(L, index))
        return EdgeInsets::all(static_cast<float>(lua_tonumber(L, index)));

    if (!lua_istable(L, index))
        luaL_errorL(L, "padding must be a number or table");

    auto readField = [&](const char* name) -> float
    {
        lua_getfield(L, index, name);
        float value = lua_isnil(L, -1) ? 0.0f : static_cast<float>(luaL_checknumber(L, -1));
        lua_pop(L, 1);
        return value;
    };

    EdgeInsets padding;
    padding.top = readField("top");
    padding.right = readField("right");
    padding.bottom = readField("bottom");
    padding.left = readField("left");
    return padding;
}

static LogicalKey readLogicalKey(lua_State* L, int index)
{
    size_t length = 0;
    const char* value = luaL_checklstring(L, index, &length);
    std::string key(value, length);
    for (char& ch : key)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    if (key == "tab")
        return LogicalKey::Tab;
    if (key == "enter" || key == "return")
        return LogicalKey::Enter;
    if (key == "space" || key == " ")
        return LogicalKey::Space;
    if (key == "escape" || key == "esc")
        return LogicalKey::Escape;

    luaL_errorL(L, "unsupported ui key '%s'", key.c_str());
    return LogicalKey::Unknown;
}

static PhysicalKey physicalKeyForLogical(LogicalKey key)
{
    switch (key)
    {
    case LogicalKey::Tab:
        return PhysicalKey::Tab;
    case LogicalKey::Enter:
        return PhysicalKey::Enter;
    case LogicalKey::Space:
        return PhysicalKey::Space;
    case LogicalKey::Escape:
        return PhysicalKey::Escape;
    case LogicalKey::Unknown:
        return PhysicalKey::Unknown;
    }

    return PhysicalKey::Unknown;
}

static Modifiers readModifiers(lua_State* L, int index)
{
    Modifiers modifiers;
    if (lua_isnoneornil(L, index))
        return modifiers;

    luaL_checktype(L, index, LUA_TTABLE);
    auto readBooleanField = [&](const char* name) -> bool
    {
        lua_getfield(L, index, name);
        bool value = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return value;
    };

    modifiers.shift = readBooleanField("shift");
    modifiers.control = readBooleanField("control");
    modifiers.alt = readBooleanField("alt");
    modifiers.meta = readBooleanField("meta");
    return modifiers;
}

static void pushElement(lua_State* L, std::shared_ptr<UiContext> context, NodeId id)
{
    void* storage = lua_newuserdatataggedwithmetatable(L, sizeof(LuaElement), kUiElementTag);
    new (storage) LuaElement{std::move(context), id};
}

static std::shared_ptr<Ref> makeRef(lua_State* L, int index)
{
    lua_pushvalue(L, index);
    std::shared_ptr<Ref> ref = std::make_shared<Ref>(L, -1);
    lua_pop(L, 1);
    return ref;
}

static void bindTextFunction(lua_State* L, const std::shared_ptr<UiContext>& context, NodeId id, int functionIndex)
{
    std::shared_ptr<Ref> ref = makeRef(L, functionIndex);
    lua_State* effectThread = L;

    auto effect = std::make_shared<Effect>(
        context->reactive(),
        [context, id, ref, effectThread]()
        {
            int top = lua_gettop(effectThread);
            ref->push(effectThread);

            if (lua_pcall(effectThread, 0, 1, 0) != LUA_OK)
            {
                const char* message = lua_tostring(effectThread, -1);
                context->setLastError(message ? message : "ui text effect failed");
                lua_settop(effectThread, top);
                return;
            }

            std::string text = valueToText(effectThread, -1);
            lua_settop(effectThread, top);
            context->nodes().setText(id, std::move(text));
        }
    );

    context->retainEffect(std::move(effect));
}

static NodeId createTextFromValue(lua_State* L, const std::shared_ptr<UiContext>& context, int index)
{
    NodeId id = context->createNode(WidgetKind::Text);
    if (lua_isfunction(L, index))
        bindTextFunction(L, context, id, index);
    else
        context->nodes().setText(id, valueToText(L, index));
    return id;
}

static void applyChildrenAndProps(lua_State* L, const std::shared_ptr<UiContext>& context, NodeId id, int index)
{
    luaL_checktype(L, index, LUA_TTABLE);

    lua_pushnil(L);
    while (lua_next(L, index) != 0)
    {
        if (lua_type(L, -2) == LUA_TNUMBER)
        {
            LuaElement* child = checkElement(L, -1);
            context->nodes().appendChild(id, child->id);
        }
        else if (lua_type(L, -2) == LUA_TSTRING)
        {
            const char* key = lua_tostring(L, -2);
            if (strcmp(key, "title") == 0)
                context->nodes().setTitle(id, valueToText(L, -1));
            else if (strcmp(key, "text") == 0)
            {
                if (lua_isfunction(L, -1))
                    bindTextFunction(L, context, id, -1);
                else
                    context->nodes().setText(id, valueToText(L, -1));
            }
            else if (strcmp(key, "gap") == 0)
                context->nodes().setGap(id, static_cast<float>(luaL_checknumber(L, -1)));
            else if (strcmp(key, "padding") == 0)
                context->nodes().setPadding(id, readPadding(L, -1));
            else if (strcmp(key, "disabled") == 0)
                context->nodes().setDisabled(id, lua_toboolean(L, -1) != 0);
            else if (strcmp(key, "onPress") == 0 || strcmp(key, "onActivate") == 0)
            {
                luaL_checktype(L, -1, LUA_TFUNCTION);
                std::shared_ptr<Ref> ref = makeRef(L, -1);
                lua_State* callbackThread = L;
                context->nodes().setOnActivate(
                    id,
                    [context, ref, callbackThread]()
                    {
                        Transaction::run(
                            context->reactive(),
                            [context, ref, callbackThread]()
                            {
                                int top = lua_gettop(callbackThread);
                                ref->push(callbackThread);
                                if (lua_pcall(callbackThread, 0, 0, 0) != LUA_OK)
                                {
                                    const char* message = lua_tostring(callbackThread, -1);
                                    context->setLastError(message ? message : "ui callback failed");
                                }
                                lua_settop(callbackThread, top);
                            }
                        );
                    }
                );
            }
        }

        lua_pop(L, 1);
    }
}

static int ui_signal(lua_State* L)
{
    LuaUiState* state = closureState(L);
    LuaValue initial = readLuaValue(L, 1);

    void* storage = lua_newuserdatataggedwithmetatable(L, sizeof(LuaSignal), kUiSignalTag);
    new (storage) LuaSignal{state->context, std::make_shared<Signal<LuaValue>>(state->context->reactive(), std::move(initial))};
    return 1;
}

static int ui_computed(lua_State* L)
{
    LuaUiState* state = closureState(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);

    auto signal = std::make_shared<Signal<LuaValue>>(state->context->reactive(), LuaValue{});
    std::shared_ptr<Ref> ref = makeRef(L, 1);
    lua_State* effectThread = L;
    std::shared_ptr<UiContext> context = state->context;

    auto effect = std::make_shared<Effect>(
        context->reactive(),
        [context, signal, ref, effectThread]()
        {
            int top = lua_gettop(effectThread);
            ref->push(effectThread);

            if (lua_pcall(effectThread, 0, 1, 0) != LUA_OK)
            {
                const char* message = lua_tostring(effectThread, -1);
                context->setLastError(message ? message : "ui computed effect failed");
                lua_settop(effectThread, top);
                return;
            }

            signal->set(readLuaValue(effectThread, -1));
            lua_settop(effectThread, top);
        }
    );

    context->retainEffect(std::move(effect));

    void* storage = lua_newuserdatataggedwithmetatable(L, sizeof(LuaSignal), kUiSignalTag);
    new (storage) LuaSignal{state->context, std::move(signal)};
    return 1;
}

static int signal_get(lua_State* L)
{
    LuaSignal* signal = checkSignal(L, 1);
    pushLuaValue(L, signal->signal->get());
    return 1;
}

static int signal_set(lua_State* L)
{
    LuaSignal* signal = checkSignal(L, 1);
    signal->signal->set(readLuaValue(L, 2));
    if (std::optional<std::string> error = signal->context->takeLastError())
        luaL_errorL(L, "%s", error->c_str());
    return 0;
}

static int signal_index(lua_State* L)
{
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "get") == 0)
    {
        lua_pushcfunction(L, signal_get, "Signal.get");
        return 1;
    }
    if (strcmp(key, "set") == 0)
    {
        lua_pushcfunction(L, signal_set, "Signal.set");
        return 1;
    }
    return 0;
}

static int element_index(lua_State* L)
{
    LuaElement* element = checkElement(L, 1);
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "id") == 0)
    {
        lua_pushinteger(L, static_cast<int>(element->id));
        return 1;
    }
    return 0;
}

static int create_node(lua_State* L, WidgetKind kind)
{
    LuaUiState* state = closureState(L);
    NodeId id = state->context->createNode(kind);

    if (!lua_isnoneornil(L, 1))
        applyChildrenAndProps(L, state->context, id, 1);

    if (kind == WidgetKind::Window)
        state->context->setRoot(id);

    pushElement(L, state->context, id);
    return 1;
}

static int ui_window(lua_State* L)
{
    return create_node(L, WidgetKind::Window);
}

static int ui_box(lua_State* L)
{
    return create_node(L, WidgetKind::Box);
}

static int ui_column(lua_State* L)
{
    return create_node(L, WidgetKind::Column);
}

static int ui_row(lua_State* L)
{
    return create_node(L, WidgetKind::Row);
}

static int ui_text(lua_State* L)
{
    LuaUiState* state = closureState(L);
    NodeId id = createTextFromValue(L, state->context, 1);
    pushElement(L, state->context, id);
    return 1;
}

static int ui_button(lua_State* L)
{
    return create_node(L, WidgetKind::Button);
}

static std::shared_ptr<UiContext> contextFromOptionalElement(lua_State* L, LuaUiState* state, int index, NodeId* id)
{
    if (lua_isnoneornil(L, index))
    {
        *id = state->context->root();
        return state->context;
    }

    LuaElement* element = checkElement(L, index);
    *id = element->id;
    return element->context;
}

static int ui_flush(lua_State* L)
{
    LuaUiState* state = closureState(L);
    NodeId id = kInvalidNodeId;
    std::shared_ptr<UiContext> context = contextFromOptionalElement(L, state, 1, &id);
    if (id != kInvalidNodeId)
        context->setRoot(id);
    context->flush();
    return 0;
}

static int ui_dump_ui_tree(lua_State* L)
{
    LuaUiState* state = closureState(L);
    NodeId id = kInvalidNodeId;
    std::shared_ptr<UiContext> context = contextFromOptionalElement(L, state, 1, &id);
    std::string dump = context->dumpUiTree(id);
    lua_pushlstring(L, dump.data(), dump.size());
    return 1;
}

static int ui_dump_layout_tree(lua_State* L)
{
    LuaUiState* state = closureState(L);
    NodeId id = kInvalidNodeId;
    std::shared_ptr<UiContext> context = contextFromOptionalElement(L, state, 1, &id);
    std::string dump = context->dumpLayoutTree(id);
    lua_pushlstring(L, dump.data(), dump.size());
    return 1;
}

static int ui_dump_scene(lua_State* L)
{
    LuaUiState* state = closureState(L);
    NodeId id = kInvalidNodeId;
    std::shared_ptr<UiContext> context = contextFromOptionalElement(L, state, 1, &id);
    std::string dump = context->dumpScene(id);
    lua_pushlstring(L, dump.data(), dump.size());
    return 1;
}

static int ui_dump_semantics(lua_State* L)
{
    LuaUiState* state = closureState(L);
    NodeId id = kInvalidNodeId;
    std::shared_ptr<UiContext> context = contextFromOptionalElement(L, state, 1, &id);
    std::string dump = context->dumpSemantics(id);
    lua_pushlstring(L, dump.data(), dump.size());
    return 1;
}

static int ui_dump_profile(lua_State* L)
{
    LuaUiState* state = closureState(L);
    NodeId id = kInvalidNodeId;
    std::shared_ptr<UiContext> context = contextFromOptionalElement(L, state, 1, &id);
    (void)id;

    std::string dump = context->dumpProfile();
    lua_pushlstring(L, dump.data(), dump.size());
    return 1;
}

static int ui_activate(lua_State* L)
{
    LuaElement* element = checkElement(L, 1);
    bool handled = element->context->activate(element->id);
    if (std::optional<std::string> error = element->context->takeLastError())
        luaL_errorL(L, "%s", error->c_str());

    lua_pushboolean(L, handled);
    return 1;
}

static int ui_focus(lua_State* L)
{
    LuaElement* element = checkElement(L, 1);
    bool handled = element->context->focus(element->id);
    lua_pushboolean(L, handled);
    return 1;
}

static int ui_focused(lua_State* L)
{
    LuaUiState* state = closureState(L);
    NodeId id = kInvalidNodeId;
    std::shared_ptr<UiContext> context = contextFromOptionalElement(L, state, 1, &id);
    (void)id;

    std::optional<NodeId> focused = context->focusedNode();
    if (!focused)
    {
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, static_cast<int>(*focused));
    return 1;
}

static int ui_click(lua_State* L)
{
    LuaElement* root = checkElement(L, 1);
    PointerEvent event;
    event.button = PointerButton::Primary;
    event.position = {static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3))};

    event.kind = PointerEventKind::Down;
    bool handled = root->context->dispatchPointer(event, root->id);
    event.kind = PointerEventKind::Up;
    handled = root->context->dispatchPointer(event, root->id) || handled;
    if (std::optional<std::string> error = root->context->takeLastError())
        luaL_errorL(L, "%s", error->c_str());

    lua_pushboolean(L, handled);
    return 1;
}

static int ui_press_key(lua_State* L)
{
    LuaElement* root = checkElement(L, 1);
    LogicalKey logical = readLogicalKey(L, 2);
    Modifiers modifiers = readModifiers(L, 3);

    KeyEvent event;
    event.physical = physicalKeyForLogical(logical);
    event.logical = logical;
    event.modifiers = modifiers;

    event.kind = KeyEventKind::Down;
    bool handled = root->context->dispatchKey(event, root->id);
    event.kind = KeyEventKind::Up;
    handled = root->context->dispatchKey(event, root->id) || handled;

    if (std::optional<std::string> error = root->context->takeLastError())
        luaL_errorL(L, "%s", error->c_str());

    lua_pushboolean(L, handled);
    return 1;
}

static int ui_render(lua_State* L)
{
    LuaUiState* state = closureState(L);
    NodeId id = kInvalidNodeId;
    std::shared_ptr<UiContext> context = contextFromOptionalElement(L, state, 1, &id);
    if (id != kInvalidNodeId)
        context->setRoot(id);

    RenderStats stats = context->render();
    lua_createtable(L, 0, 3);
    lua_pushstring(L, stats.backend.c_str());
    lua_setfield(L, -2, "backend");
    lua_pushinteger(L, static_cast<int>(stats.displayItemCount));
    lua_setfield(L, -2, "displayItemCount");
    lua_pushinteger(L, static_cast<int>(stats.sceneGeneration));
    lua_setfield(L, -2, "sceneGeneration");
    return 1;
}

static int ui_run(lua_State* L)
{
    LuaUiState* state = closureState(L);
    NodeId id = kInvalidNodeId;
    std::shared_ptr<UiContext> context = contextFromOptionalElement(L, state, 1, &id);
    if (id != kInvalidNodeId)
        context->setRoot(id);

    std::string error;
    if (!runNativeShell(context, &error))
        luaL_errorL(L, "%s", error.c_str());

    return 0;
}

} // namespace lute::ui

static void registerUiMetatables(lua_State* L)
{
    luaL_newmetatable(L, "UiContext");
    lua_pushstring(L, "UiContext");
    lua_setfield(L, -2, "__type");
    lua_setuserdatadtor(
        L,
        kUiContextTag,
        [](lua_State*, void* ud)
        {
            std::destroy_at(static_cast<lute::ui::LuaUiState*>(ud));
        }
    );
    lua_setuserdatametatable(L, kUiContextTag);

    luaL_newmetatable(L, "UiElement");
    lua_pushcfunction(L, lute::ui::element_index, "UiElement.__index");
    lua_setfield(L, -2, "__index");
    lua_pushstring(L, "UiElement");
    lua_setfield(L, -2, "__type");
    lua_setuserdatadtor(
        L,
        kUiElementTag,
        [](lua_State*, void* ud)
        {
            std::destroy_at(static_cast<lute::ui::LuaElement*>(ud));
        }
    );
    lua_setuserdatametatable(L, kUiElementTag);

    luaL_newmetatable(L, "UiSignal");
    lua_pushcfunction(L, lute::ui::signal_index, "UiSignal.__index");
    lua_setfield(L, -2, "__index");
    lua_pushstring(L, "UiSignal");
    lua_setfield(L, -2, "__type");
    lua_setuserdatadtor(
        L,
        kUiSignalTag,
        [](lua_State*, void* ud)
        {
            std::destroy_at(static_cast<lute::ui::LuaSignal*>(ud));
        }
    );
    lua_setuserdatametatable(L, kUiSignalTag);
}

const char* const UI::properties[] = {nullptr};

const luaL_Reg UI::lib[] = {
    {"signal", lute::ui::ui_signal},
    {"computed", lute::ui::ui_computed},
    {"window", lute::ui::ui_window},
    {"box", lute::ui::ui_box},
    {"column", lute::ui::ui_column},
    {"row", lute::ui::ui_row},
    {"text", lute::ui::ui_text},
    {"button", lute::ui::ui_button},
    {"flush", lute::ui::ui_flush},
    {"render", lute::ui::ui_render},
    {"run", lute::ui::ui_run},
    {"activate", lute::ui::ui_activate},
    {"focus", lute::ui::ui_focus},
    {"focused", lute::ui::ui_focused},
    {"click", lute::ui::ui_click},
    {"press_key", lute::ui::ui_press_key},
    {"dump_ui_tree", lute::ui::ui_dump_ui_tree},
    {"dump_layout_tree", lute::ui::ui_dump_layout_tree},
    {"dump_scene", lute::ui::ui_dump_scene},
    {"dump_semantics", lute::ui::ui_dump_semantics},
    {"dump_profile", lute::ui::ui_dump_profile},
    {nullptr, nullptr},
};

int UI::pushLibrary(lua_State* L)
{
    registerUiMetatables(L);

    lua_createtable(L, 0, 20);
    int tableIndex = lua_gettop(L);

    void* storage = lua_newuserdatataggedwithmetatable(L, sizeof(lute::ui::LuaUiState), kUiContextTag);
    new (storage) lute::ui::LuaUiState();
    int stateIndex = lua_gettop(L);

    for (auto& [name, func] : UI::lib)
    {
        if (!name || !func)
            break;

        lua_pushvalue(L, stateIndex);
        lua_pushcclosure(L, func, name, 1);
        lua_setfield(L, tableIndex, name);
    }

    lua_setfield(L, tableIndex, "__state");
    lua_setreadonly(L, tableIndex, 1);

    return 1;
}

LUTE_API int luaopen_ui(lua_State* L)
{
    return UI::openAsGlobal(L);
}

LUTE_API int luteopen_ui(lua_State* L)
{
    return UI::pushLibrary(L);
}
