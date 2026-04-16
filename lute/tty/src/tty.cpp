#include "lute/tty.h"

#include "lute/runtime.h"

#include "Luau/Variant.h"

#include "lua.h"
#include "lualib.h"

#include "uv.h"

#include <memory>
#include <string>

namespace
{

struct TTYReadHandle
{
    uv_tty_t tty;
    uv_loop_t* loop = nullptr;
    ResumeToken resumeToken;
    std::shared_ptr<TTYReadHandle> self;
    std::vector<char> buffer;

    static void closeCb(uv_handle_t* handle)
    {
        TTYReadHandle* ttyh = static_cast<TTYReadHandle*>(handle->data);
        ttyh->self.reset();
    }

    void close()
    {
        uv_read_stop(reinterpret_cast<uv_stream_t*>(&tty));
        uv_close(reinterpret_cast<uv_handle_t*>(&tty), closeCb);
    }
};

struct TTYState
{
    uv_tty_t stdinTty;
    uv_tty_t stdoutTty;
    uv_loop_t* loop = nullptr;
    bool stdinInitialized = false;
    bool stdoutInitialized = false;
    bool rawModeEnabled = false;
    bool exitHandlerRegistered = false;
};

TTYState gTTYState;

void resetTtyModeAtExit()
{
    uv_tty_reset_mode();
}

bool inputIsTTY()
{
    return uv_guess_handle(fileno(stdin)) == UV_TTY;
}

bool outputIsTTY()
{
    return uv_guess_handle(fileno(stdout)) == UV_TTY;
}

void ensureInputTTY(lua_State* L)
{
    if (!inputIsTTY())
        luaL_error(L, "stdin is not attached to a TTY");

    if (!gTTYState.stdinInitialized)
    {
        gTTYState.loop = getRuntimeLoop(L);
        int status = uv_tty_init(gTTYState.loop, &gTTYState.stdinTty, fileno(stdin), 0);
        if (status < 0)
            luaL_error(L, "Failed to initialize stdin TTY: %s", uv_strerror(status));
        gTTYState.stdinInitialized = true;
    }
}

void ensureOutputTTY(lua_State* L)
{
    if (!outputIsTTY())
        luaL_error(L, "stdout is not attached to a TTY");

    if (!gTTYState.stdoutInitialized)
    {
        gTTYState.loop = getRuntimeLoop(L);
        int status = uv_tty_init(gTTYState.loop, &gTTYState.stdoutTty, fileno(stdout), 0);
        if (status < 0)
            luaL_error(L, "Failed to initialize stdout TTY: %s", uv_strerror(status));
        gTTYState.stdoutInitialized = true;
    }
}

void allocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf)
{
    TTYReadHandle* ttyh = static_cast<TTYReadHandle*>(handle->data);
    ttyh->buffer.resize(suggestedSize);
    buf->base = ttyh->buffer.data();
    buf->len = ttyh->buffer.size();
}

void onTTYRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    TTYReadHandle* handle = static_cast<TTYReadHandle*>(stream->data);

    if (nread > 0)
    {
        handle->resumeToken->complete(
            [data = std::string(buf->base, nread)](lua_State* L) -> int
            {
                lua_pushlstring(L, data.c_str(), data.size());
                return 1;
            }
        );
    }
    else if (nread < 0)
    {
        handle->resumeToken->fail(uv_strerror(nread));
    }

    handle->close();
}

int lua_inputIsTTY(lua_State* L)
{
    lua_pushboolean(L, inputIsTTY());
    return 1;
}

int lua_outputIsTTY(lua_State* L)
{
    lua_pushboolean(L, outputIsTTY());
    return 1;
}

int lua_isRawMode(lua_State* L)
{
    lua_pushboolean(L, gTTYState.rawModeEnabled);
    return 1;
}

int lua_setRawMode(lua_State* L)
{
    bool enable = lua_toboolean(L, 1);

    if (enable == gTTYState.rawModeEnabled)
        return 0;

    ensureInputTTY(L);

    if (enable)
    {
        if (!gTTYState.exitHandlerRegistered)
        {
            std::atexit(resetTtyModeAtExit);
            gTTYState.exitHandlerRegistered = true;
        }

        int status = uv_tty_set_mode(&gTTYState.stdinTty, UV_TTY_MODE_RAW);
        if (status < 0)
            luaL_error(L, "Failed to enable raw mode: %s", uv_strerror(status));
        gTTYState.rawModeEnabled = true;
        return 0;
    }

    int status = uv_tty_set_mode(&gTTYState.stdinTty, UV_TTY_MODE_NORMAL);
    if (status < 0)
        luaL_error(L, "Failed to disable raw mode: %s", uv_strerror(status));
    gTTYState.rawModeEnabled = false;
    return 0;
}

int lua_getWindowSize(lua_State* L)
{
    ensureOutputTTY(L);

    int width = 0;
    int height = 0;
    int status = uv_tty_get_winsize(&gTTYState.stdoutTty, &width, &height);
    if (status < 0)
        luaL_error(L, "Failed to get TTY window size: %s", uv_strerror(status));

    lua_createtable(L, 0, 2);

    lua_pushinteger(L, height);
    lua_setfield(L, -2, "rows");

    lua_pushinteger(L, width);
    lua_setfield(L, -2, "cols");

    return 1;
}

int lua_read(lua_State* L)
{
    ensureInputTTY(L);

    auto handle = std::make_shared<TTYReadHandle>();
    handle->loop = getRuntimeLoop(L);
    handle->resumeToken = getResumeToken(L);
    handle->self = handle;

    int status = uv_tty_init(handle->loop, &handle->tty, fileno(stdin), 0);
    if (status < 0)
        luaL_error(L, "Failed to initialize stdin TTY: %s", uv_strerror(status));

    handle->tty.data = handle.get();
    uv_read_start(reinterpret_cast<uv_stream_t*>(&handle->tty), allocBuffer, onTTYRead);
    return lua_yield(L, 0);
}

} // namespace

const char* const TTY::properties[] = {nullptr};

const luaL_Reg TTY::lib[] = {
    {"inputIsTTY", lua_inputIsTTY},
    {"outputIsTTY", lua_outputIsTTY},
    {"isRawMode", lua_isRawMode},
    {"setRawMode", lua_setRawMode},
    {"getWindowSize", lua_getWindowSize},
    {"read", lua_read},
    {nullptr, nullptr},
};

int TTY::pushLibrary(lua_State* L)
{
    lua_createtable(L, 0, std::size(TTY::lib));

    for (auto& [name, func] : TTY::lib)
    {
        if (!name || !func)
            break;

        lua_pushcfunction(L, func, name);
        lua_setfield(L, -2, name);
    }

    lua_setreadonly(L, -1, 1);

    return 1;
}

int luaopen_tty(lua_State* L)
{
    return TTY::openAsGlobal(L);
}

int luteopen_tty(lua_State* L)
{
    return TTY::pushLibrary(L);
}
