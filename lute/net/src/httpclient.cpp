#include "lute/common.h"
#include "lute/runtime.h"

#include "Luau/DenseHash.h"

#include "lua.h"
#include "lualib.h"

#include "curl/curl.h"

#include <cctype>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "system_ca.h"

namespace net::client
{

static const std::string kEmptyHeaderKey = "";
static constexpr long kDefaultRequestTimeoutMs = 5 * 60 * 1000;
static constexpr long kDefaultConnectTimeoutMs = 30 * 1000;

struct CurlResponse
{
    std::vector<char> body;
    Luau::DenseHashMap<std::string, std::string> headers;
    long status = 0;

    CurlResponse()
        : headers(kEmptyHeaderKey)
    {
    }
};

struct CurlMultiManager;
struct ClientBodyState;
static void resumeClientBody(const std::shared_ptr<ClientBodyState>& bodyState);

enum class ClientBodyStatus
{
    NotRequested,
    Buffering,
    Complete,
    Canceled,
    Errored,
};

struct ClientBodyState
{
    Runtime* runtime = nullptr;
    CurlMultiManager* manager = nullptr;
    CURL* easy = nullptr;
    ClientBodyStatus status = ClientBodyStatus::NotRequested;
    std::vector<char> body;
    Luau::DenseHashMap<std::string, std::string> headers;
    long responseStatus = 0;
    std::string error;

    ClientBodyState()
        : headers(kEmptyHeaderKey)
    {
    }

    ~ClientBodyState();
};

struct HttpRequestState
{
    CURL* easy = nullptr;
    CurlMultiManager* manager = nullptr;
    curl_slist* headerList = nullptr;
    std::string url;
    std::string method;
    std::string body;
    CurlResponse response;
    CurlResponse currentHeaderBlock;
    char errorBuffer[CURL_ERROR_SIZE] = {};
    ResumeToken token;
    std::shared_ptr<ClientBodyState> bodyState;
    bool responseDelivered = false;
    bool receivePaused = false;
    bool currentHeaderHasLocation = false;

    ~HttpRequestState()
    {
        if (headerList)
            curl_slist_free_all(headerList);

        if (easy)
            curl_easy_cleanup(easy);
    }
};

struct CurlSocketState
{
    curl_socket_t socket = CURL_SOCKET_BAD;
    uv_poll_t poll{};
    CurlMultiManager* manager = nullptr;
    int activeEvents = 0;
    bool closing = false;
};

static size_t writeFunction(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* state = static_cast<HttpRequestState*>(userdata);
    LUTE_ASSERT(state);

    std::vector<char>* target = state->bodyState ? &state->bodyState->body : &state->response.body;
    LUTE_ASSERT(target);

    size_t fullsize = size * nmemb;
    if (state->bodyState && state->bodyState->status == ClientBodyStatus::NotRequested)
    {
        state->receivePaused = true;
        return CURL_WRITEFUNC_PAUSE;
    }

    target->insert(target->end(), ptr, ptr + fullsize);
    return fullsize;
}

static std::string trimHeaderValue(std::string_view value)
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);

    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);

    return std::string(value);
}

static bool headerNameEquals(const std::string& lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (size_t i = 0; i < lhs.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) != rhs[i])
            return false;
    }

    return true;
}

static bool hasContentLengthZero(const Luau::DenseHashMap<std::string, std::string>& headers)
{
    for (const auto& header : headers)
    {
        if (!headerNameEquals(header.first, "content-length"))
            continue;

        bool sawDigit = false;
        for (unsigned char ch : header.second)
        {
            if (!std::isdigit(ch))
                continue;

            sawDigit = true;
            if (ch != '0')
                return false;
        }

        if (sawDigit)
            return true;
    }

    return false;
}

static bool responseHasNoBody(const HttpRequestState& state)
{
    long status = state.response.status;
    return state.method == "HEAD" || status == 204 || status == 304 || hasContentLengthZero(state.response.headers);
}

static void rawSetBodyField(lua_State* L, int tableIndex, const std::vector<char>& body)
{
    tableIndex = lua_absindex(L, tableIndex);
    lua_pushstring(L, "body");
    lua_pushlstring(L, body.empty() ? "" : body.data(), body.size());
    lua_rawset(L, tableIndex);
}

static void completeClientBody(const std::shared_ptr<ClientBodyState>& bodyState)
{
    if (!bodyState || bodyState->status == ClientBodyStatus::Complete)
        return;

    bodyState->status = ClientBodyStatus::Complete;
    bodyState->manager = nullptr;
    bodyState->easy = nullptr;
}

static void failClientBody(const std::shared_ptr<ClientBodyState>& bodyState, std::string error, ClientBodyStatus status = ClientBodyStatus::Errored)
{
    if (!bodyState || bodyState->status == ClientBodyStatus::Complete || bodyState->status == ClientBodyStatus::Canceled ||
        bodyState->status == ClientBodyStatus::Errored)
    {
        return;
    }

    bodyState->status = status;
    bodyState->error = std::move(error);
    bodyState->manager = nullptr;
    bodyState->easy = nullptr;
}

static int response_body_index(lua_State* L)
{
    if (!lua_isstring(L, 2))
        return 0;

    const char* key = lua_tostring(L, 2);
    if (strcmp(key, "body") != 0)
        return 0;

    auto* storage = static_cast<std::shared_ptr<ClientBodyState>*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!storage || !(*storage))
        luaL_error(L, "response body state is unavailable");

    std::shared_ptr<ClientBodyState> bodyState = *storage;

    if (bodyState->status == ClientBodyStatus::Complete)
    {
        rawSetBodyField(L, 1, bodyState->body);
        lua_pushlstring(L, bodyState->body.empty() ? "" : bodyState->body.data(), bodyState->body.size());
        return 1;
    }

    if (bodyState->status == ClientBodyStatus::Canceled || bodyState->status == ClientBodyStatus::Errored)
        luaL_error(L, "%s", bodyState->error.empty() ? "response body is unavailable" : bodyState->error.c_str());

    if (bodyState->status == ClientBodyStatus::NotRequested)
    {
        bodyState->status = ClientBodyStatus::Buffering;
        resumeClientBody(bodyState);
    }

    while (bodyState->status == ClientBodyStatus::Buffering && bodyState->runtime && bodyState->runtime->hasWork())
        bodyState->runtime->runOnce();

    if (bodyState->status == ClientBodyStatus::Complete)
    {
        rawSetBodyField(L, 1, bodyState->body);
        lua_pushlstring(L, bodyState->body.empty() ? "" : bodyState->body.data(), bodyState->body.size());
        return 1;
    }

    luaL_error(L, "%s", bodyState->error.empty() ? "response body is unavailable" : bodyState->error.c_str());
}

static int pushResponse(lua_State* L, const std::shared_ptr<ClientBodyState>& bodyState)
{
    lua_createtable(L, 0, 4);

    lua_pushstring(L, "headers");
    lua_createtable(L, 0, bodyState->headers.size());
    for (const auto& header : bodyState->headers)
    {
        lua_pushlstring(L, header.first.data(), header.first.size());
        lua_pushlstring(L, header.second.data(), header.second.size());
        lua_settable(L, -3);
    }
    lua_settable(L, -3);

    lua_pushstring(L, "status");
    lua_pushinteger(L, bodyState->responseStatus);
    lua_settable(L, -3);

    lua_pushstring(L, "ok");
    lua_pushboolean(L, (bodyState->responseStatus >= 200 && bodyState->responseStatus < 300));
    lua_settable(L, -3);

    int responseIndex = lua_absindex(L, -1);
    lua_createtable(L, 0, 1);
    lua_pushstring(L, "__index");
    auto* storage = new (lua_newuserdatadtor(
        L,
        sizeof(std::shared_ptr<ClientBodyState>),
        [](void* ptr)
        {
            std::destroy_at(static_cast<std::shared_ptr<ClientBodyState>*>(ptr));
        }
    )) std::shared_ptr<ClientBodyState>(bodyState);
    (void)storage;
    lua_pushcclosure(L, response_body_index, "response.__index", 1);
    lua_settable(L, -3);
    lua_setmetatable(L, responseIndex);

    return 1;
}

static bool parseStatusLine(std::string_view line, long& status)
{
    if (line.rfind("HTTP/", 0) != 0)
        return false;

    size_t firstSpace = line.find(' ');
    if (firstSpace == std::string_view::npos)
        return false;

    while (firstSpace < line.size() && line[firstSpace] == ' ')
        firstSpace++;

    long parsed = 0;
    size_t digits = 0;
    while (firstSpace + digits < line.size() && std::isdigit(static_cast<unsigned char>(line[firstSpace + digits])))
    {
        parsed = parsed * 10 + (line[firstSpace + digits] - '0');
        digits++;
    }

    if (digits != 3)
        return false;

    status = parsed;
    return true;
}

static bool isRedirectWithLocation(const HttpRequestState& state)
{
    return state.currentHeaderBlock.status >= 300 && state.currentHeaderBlock.status < 400 && state.currentHeaderHasLocation;
}

static size_t headerFunction(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* state = static_cast<HttpRequestState*>(userdata);
    LUTE_ASSERT(state);

    size_t fullsize = size * nmemb;
    std::string_view line(ptr, fullsize);

    long status = 0;
    if (parseStatusLine(line, status))
    {
        state->currentHeaderBlock = CurlResponse();
        state->currentHeaderBlock.status = status;
        state->currentHeaderHasLocation = false;
        return fullsize;
    }

    if (line == "\r\n" || line == "\n")
    {
        if (state->responseDelivered || state->currentHeaderBlock.status == 0 || state->currentHeaderBlock.status < 200 ||
            isRedirectWithLocation(*state))
        {
            return fullsize;
        }

        state->response = std::move(state->currentHeaderBlock);
        state->bodyState->headers = state->response.headers;
        state->bodyState->responseStatus = state->response.status;
        state->bodyState->manager = state->manager;
        state->bodyState->easy = state->easy;

        state->responseDelivered = true;

        bool noBody = responseHasNoBody(*state);
        if (noBody)
        {
            completeClientBody(state->bodyState);
        }
        else
        {
            CURLcode pauseResult = curl_easy_pause(state->easy, CURLPAUSE_RECV);
            if (pauseResult == CURLE_OK)
            {
                state->receivePaused = true;
            }
        }

        state->token->complete(
            [bodyState = state->bodyState](lua_State* L)
            {
                return pushResponse(L, bodyState);
            }
        );

        return fullsize;
    }

    size_t colon = line.find(':');
    if (colon == std::string_view::npos)
        return fullsize;

    std::string name(line.data(), colon);
    std::string value = trimHeaderValue(line.substr(colon + 1));

    if (headerNameEquals(name, "location"))
        state->currentHeaderHasLocation = true;

    if (state->currentHeaderBlock.headers.contains(name))
        state->currentHeaderBlock.headers[name] += ", " + value;
    else
        state->currentHeaderBlock.headers[name] = value;

    return fullsize;
}

struct CurlMultiManager
{
    explicit CurlMultiManager(Runtime* runtime)
        : runtime(runtime)
    {
        multi = curl_multi_init();
        if (!multi)
        {
            initError = "failed to initialize curl multi handle";
            return;
        }

        int timerResult = uv_timer_init(runtime->getEventLoop(), &timer);
        if (timerResult != 0)
        {
            initError = std::string("failed to initialize curl timer: ") + uv_strerror(timerResult);
            return;
        }

        timerInitialized = true;
        timer.data = this;

        curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, &CurlMultiManager::socketCallback);
        curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, this);
        curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, &CurlMultiManager::timerCallback);
        curl_multi_setopt(multi, CURLMOPT_TIMERDATA, this);
    }

    Runtime* runtime = nullptr;
    CURLM* multi = nullptr;
    uv_timer_t timer{};
    bool timerInitialized = false;
    bool timerClosing = false;
    bool closing = false;
    bool deleteWhenClosed = false;
    int pendingCloseHandles = 0;
    std::string initError;
    std::unordered_map<curl_socket_t, CurlSocketState*> sockets;
    std::unordered_map<CURL*, std::unique_ptr<HttpRequestState>> requests;

    bool ok() const
    {
        return initError.empty();
    }

    bool addRequest(std::unique_ptr<HttpRequestState> state, std::string& error)
    {
        if (closing || !multi)
        {
            error = "curl multi manager is closing";
            return false;
        }

        CURL* easy = state->easy;
        state->manager = this;
        if (state->bodyState)
        {
            state->bodyState->manager = this;
            state->bodyState->easy = easy;
        }
        requests[easy] = std::move(state);

        CURLMcode result = curl_multi_add_handle(multi, easy);
        if (result != CURLM_OK)
        {
            auto it = requests.find(easy);
            if (it != requests.end())
                requests.erase(it);

            error = curl_multi_strerror(result);
            return false;
        }

        return true;
    }

    void socketAction(curl_socket_t socket, int action)
    {
        if (closing || !multi)
            return;

        int runningHandles = 0;
        CURLMcode result = curl_multi_socket_action(multi, socket, action, &runningHandles);
        if (result != CURLM_OK)
        {
            failAll(std::string("curl multi socket action failed: ") + curl_multi_strerror(result));
            return;
        }

        drainCompletions();
    }

    void drainCompletions()
    {
        if (closing || !multi)
            return;

        int messagesLeft = 0;
        CURLMsg* message = nullptr;
        while (!closing && multi && (message = curl_multi_info_read(multi, &messagesLeft)))
        {
            if (message->msg != CURLMSG_DONE)
                continue;

            completeRequest(message->easy_handle, message->data.result);
        }
    }

    void completeRequest(CURL* easy, CURLcode result)
    {
        auto it = requests.find(easy);
        if (it == requests.end())
            return;

        curl_multi_remove_handle(multi, easy);

        std::unique_ptr<HttpRequestState> state = std::move(it->second);
        requests.erase(it);

        if (result != CURLE_OK)
        {
            std::string error = state->errorBuffer[0] != '\0' ? state->errorBuffer : curl_easy_strerror(result);
            if (state->responseDelivered)
                failClientBody(state->bodyState, "network request failed: " + error);
            else
                state->token->fail("network request failed: " + error);
        }
        else
        {
            if (!state->responseDelivered)
            {
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &state->response.status);
                state->bodyState->headers = state->response.headers;
                state->bodyState->responseStatus = state->response.status;
                state->bodyState->body = std::move(state->response.body);
                completeClientBody(state->bodyState);

                state->token->complete(
                    [bodyState = state->bodyState](lua_State* L)
                    {
                        return pushResponse(L, bodyState);
                    }
                );
            }
            else
            {
                completeClientBody(state->bodyState);
            }
        }

        if (state->bodyState)
        {
            state->bodyState->manager = nullptr;
            state->bodyState->easy = nullptr;
        }

        maybeDestroyWhenIdle();
    }

    void cancelRequest(CURL* easy, std::string error)
    {
        auto it = requests.find(easy);
        if (it == requests.end())
            return;

        if (multi)
            curl_multi_remove_handle(multi, easy);

        std::unique_ptr<HttpRequestState> state = std::move(it->second);
        requests.erase(it);

        if (state->bodyState)
        {
            state->bodyState->status = ClientBodyStatus::Canceled;
            state->bodyState->error = std::move(error);
            state->bodyState->manager = nullptr;
            state->bodyState->easy = nullptr;
        }

        maybeDestroyWhenIdle();
    }

    void resumeBody(ClientBodyState* bodyState)
    {
        if (!bodyState || !bodyState->easy)
            return;

        auto it = requests.find(bodyState->easy);
        if (it == requests.end())
        {
            bodyState->status = ClientBodyStatus::Errored;
            bodyState->error = "network response body is unavailable";
            bodyState->manager = nullptr;
            bodyState->easy = nullptr;
            return;
        }

        HttpRequestState* state = it->second.get();
        if (state->receivePaused)
        {
            state->receivePaused = false;
            CURLcode pauseResult = curl_easy_pause(bodyState->easy, CURLPAUSE_RECV_CONT);
            if (pauseResult != CURLE_OK)
            {
                CURL* easy = bodyState->easy;
                failClientBody(state->bodyState, std::string("network request failed: ") + curl_easy_strerror(pauseResult));
                cancelRequest(easy, "network request cancelled");
                return;
            }
        }

        socketAction(CURL_SOCKET_TIMEOUT, 0);
    }

    CurlSocketState* createSocketState(curl_socket_t socket)
    {
        auto* state = new CurlSocketState();
        state->socket = socket;
        state->manager = this;
        state->poll.data = state;

        int result = uv_poll_init_socket(runtime->getEventLoop(), &state->poll, socket);
        if (result != 0)
        {
            delete state;
            return nullptr;
        }

        sockets[socket] = state;
        curl_multi_assign(multi, socket, state);
        return state;
    }

    bool updateSocket(CurlSocketState* state, int events)
    {
        if (!state || state->closing)
            return false;

        if (state->activeEvents == events)
            return true;

        if (events == 0)
        {
            uv_poll_stop(&state->poll);
            state->activeEvents = 0;
            return true;
        }

        int result = uv_poll_start(
            &state->poll,
            events,
            [](uv_poll_t* handle, int status, int events)
            {
                auto* state = static_cast<CurlSocketState*>(handle->data);
                if (!state || state->closing || !state->manager)
                    return;

                int action = 0;
                if (status < 0)
                    action = CURL_CSELECT_ERR;
                else
                {
                    if (events & UV_READABLE)
                        action |= CURL_CSELECT_IN;
                    if (events & UV_WRITABLE)
                        action |= CURL_CSELECT_OUT;
                }

                state->manager->socketAction(state->socket, action);
            }
        );

        if (result != 0)
            return false;

        state->activeEvents = events;
        return true;
    }

    void closeSocket(CurlSocketState* state)
    {
        if (!state || state->closing)
            return;

        state->closing = true;
        sockets.erase(state->socket);

        if (multi)
            curl_multi_assign(multi, state->socket, nullptr);

        uv_poll_stop(&state->poll);
        pendingCloseHandles++;
        uv_close(
            reinterpret_cast<uv_handle_t*>(&state->poll),
            [](uv_handle_t* handle)
            {
                auto* state = static_cast<CurlSocketState*>(handle->data);
                CurlMultiManager* manager = state ? state->manager : nullptr;
                delete state;

                if (manager)
                    manager->onHandleClosed();
            }
        );
    }

    void failAll(std::string error)
    {
        if (multi)
        {
            for (auto& request : requests)
                curl_multi_remove_handle(multi, request.first);
        }

        std::vector<std::unique_ptr<HttpRequestState>> pending;
        pending.reserve(requests.size());
        for (auto& request : requests)
            pending.push_back(std::move(request.second));
        requests.clear();

        for (auto& request : pending)
        {
            if (request->responseDelivered)
                failClientBody(request->bodyState, error);
            else
                request->token->fail(error);
        }

        maybeDestroyWhenIdle();
    }

    void beginClose(bool deleteAfterClose)
    {
        if (closing)
            return;

        closing = true;
        deleteWhenClosed = deleteAfterClose;

        if (!requests.empty())
            failAll("network request cancelled");

        std::vector<CurlSocketState*> socketStates;
        socketStates.reserve(sockets.size());
        for (auto& socket : sockets)
            socketStates.push_back(socket.second);

        for (CurlSocketState* state : socketStates)
            closeSocket(state);

        if (timerInitialized && !timerClosing)
        {
            uv_timer_stop(&timer);
            timerClosing = true;
            pendingCloseHandles++;
            uv_close(
                reinterpret_cast<uv_handle_t*>(&timer),
                [](uv_handle_t* handle)
                {
                    auto* manager = static_cast<CurlMultiManager*>(handle->data);
                    if (manager)
                        manager->onHandleClosed();
                }
            );
        }

        if (multi)
        {
            curl_multi_cleanup(multi);
            multi = nullptr;
        }

        finishCloseIfReady();
    }

    void onHandleClosed()
    {
        pendingCloseHandles--;
        finishCloseIfReady();
    }

    void finishCloseIfReady()
    {
        if (deleteWhenClosed && pendingCloseHandles == 0)
            delete this;
    }

    void maybeDestroyWhenIdle();

    static int socketCallback(CURL*, curl_socket_t socket, int what, void* userp, void* socketp)
    {
        auto* manager = static_cast<CurlMultiManager*>(userp);
        if (!manager || manager->closing)
            return 0;

        auto* state = static_cast<CurlSocketState*>(socketp);

        if (what == CURL_POLL_REMOVE)
        {
            manager->closeSocket(state);
            return 0;
        }

        if (!state)
        {
            state = manager->createSocketState(socket);
            if (!state)
                return -1;
        }

        int events = 0;
        if (what == CURL_POLL_IN || what == CURL_POLL_INOUT)
            events |= UV_READABLE;
        if (what == CURL_POLL_OUT || what == CURL_POLL_INOUT)
            events |= UV_WRITABLE;

        return manager->updateSocket(state, events) ? 0 : -1;
    }

    static int timerCallback(CURLM*, long timeoutMs, void* userp)
    {
        auto* manager = static_cast<CurlMultiManager*>(userp);
        if (!manager || manager->closing || !manager->timerInitialized)
            return 0;

        if (timeoutMs < 0)
        {
            uv_timer_stop(&manager->timer);
            return 0;
        }

        uint64_t delay = timeoutMs == 0 ? 0 : static_cast<uint64_t>(timeoutMs);
        uv_timer_start(
            &manager->timer,
            [](uv_timer_t* handle)
            {
                auto* manager = static_cast<CurlMultiManager*>(handle->data);
                if (manager)
                    manager->socketAction(CURL_SOCKET_TIMEOUT, 0);
            },
            delay,
            0
        );

        return 0;
    }
};

static std::unordered_map<Runtime*, CurlMultiManager*> curlManagers;

void CurlMultiManager::maybeDestroyWhenIdle()
{
    if (!requests.empty())
        return;

    auto it = curlManagers.find(runtime);
    if (it != curlManagers.end() && it->second == this)
        curlManagers.erase(it);

    beginClose(true);
}

ClientBodyState::~ClientBodyState()
{
    if (manager && easy && status == ClientBodyStatus::NotRequested)
        manager->cancelRequest(easy, "network response body was not read");
}

static void resumeClientBody(const std::shared_ptr<ClientBodyState>& bodyState)
{
    if (!bodyState || !bodyState->manager || !bodyState->easy)
    {
        failClientBody(bodyState, "network response body is unavailable");
        return;
    }

    bodyState->manager->resumeBody(bodyState.get());
}

static CurlMultiManager* getCurlMultiManager(Runtime* runtime, std::string& error)
{
    auto it = curlManagers.find(runtime);
    if (it != curlManagers.end())
        return it->second;

    auto* manager = new CurlMultiManager(runtime);
    if (!manager->ok())
    {
        error = manager->initError;
        manager->beginClose(true);
        return nullptr;
    }

    curlManagers[runtime] = manager;
    return manager;
}

static bool isValidHeaderNameChar(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
           c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
           c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}

static bool isValidHeaderName(const std::string& name)
{
    if (name.empty())
        return false;

    for (unsigned char c : name)
    {
        if (!isValidHeaderNameChar(c))
            return false;
    }

    return true;
}

static bool isValidHeaderValue(const std::string& value)
{
    for (unsigned char c : value)
    {
        if (c == '\t')
            continue;

        if (c >= 0x20 && c <= 0x7e)
            continue;

        return false;
    }

    return true;
}

static std::unique_ptr<HttpRequestState> createRequestState(
    std::string url,
    std::string method,
    std::string body,
    std::vector<std::pair<std::string, std::string>> headers,
    ResumeToken token,
    std::string& error
)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        error = "failed to initialize curl easy handle";
        return nullptr;
    }

    auto state = std::make_unique<HttpRequestState>();
    state->easy = curl;
    state->url = std::move(url);
    state->method = std::move(method);
    state->body = std::move(body);
    state->token = std::move(token);
    state->bodyState = std::make_shared<ClientBodyState>();
    state->bodyState->runtime = state->token->runtime;
    state->bodyState->easy = curl;

    curl_easy_setopt(curl, CURLOPT_URL, state->url.c_str());
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, state->errorBuffer);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerFunction);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, state.get());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, state.get());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, kDefaultRequestTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kDefaultConnectTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 20L);
    applySystemCA(curl);

    if (state->method == "HEAD")
    {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }

    if (state->method != "GET")
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, state->method.c_str());

    if (!state->body.empty())
    {
        auto maxCurlOff = (std::numeric_limits<curl_off_t>::max)();
        if (state->body.size() > static_cast<size_t>(maxCurlOff))
        {
            error = "request body is too large";
            return nullptr;
        }

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, state->body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(state->body.size()));
    }

    for (const auto& headerPair : headers)
    {
        std::string headerString = headerPair.first + ": " + headerPair.second;
        curl_slist* next = curl_slist_append(state->headerList, headerString.c_str());
        if (!next)
        {
            error = "failed to append request header";
            return nullptr;
        }
        state->headerList = next;
    }

    if (state->headerList)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, state->headerList);

    return state;
}

int request(lua_State* L)
{
    std::string url = luaL_checkstring(L, 1);
    std::string method = "GET";
    std::string body = "";
    std::vector<std::pair<std::string, std::string>> headers;

    if (lua_istable(L, 2))
    {
        lua_getfield(L, 2, "method");
        if (lua_isstring(L, -1))
            method = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "body");
        if (lua_isstring(L, -1))
        {
            size_t len;
            const char* data = lua_tolstring(L, -1, &len);
            body.assign(data, data + len);
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "headers");
        if (lua_istable(L, -1))
        {
            lua_pushnil(L);
            while (lua_next(L, -2))
            {
                if (lua_type(L, -2) == LUA_TSTRING && lua_isstring(L, -1))
                {
                    size_t keyLen = 0;
                    size_t valueLen = 0;
                    const char* keyData = lua_tolstring(L, -2, &keyLen);
                    const char* valueData = lua_tolstring(L, -1, &valueLen);
                    std::string key(keyData, keyLen);
                    std::string value(valueData, valueLen);
                    headers.emplace_back(key, value);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    if ((method == "GET" || method == "HEAD") && !body.empty())
        luaL_error(L, "%s requests cannot include a body", method.c_str());

    for (const auto& header : headers)
    {
        if (!isValidHeaderName(header.first))
            luaL_error(L, "invalid request header name");

        if (!isValidHeaderValue(header.second))
            luaL_error(L, "invalid request header value");
    }

    auto token = getResumeToken(L);

    std::string error;
    std::unique_ptr<HttpRequestState> state =
        createRequestState(std::move(url), std::move(method), std::move(body), std::move(headers), token, error);

    if (!state)
    {
        token->fail("network request failed: " + error);
        return lua_yield(L, 0);
    }

    ThreadCompletionHandler cleanup;
    cleanup.consumesErrors = false;
    cleanup.onFinish = [](lua_State* L, int)
    {
        lua_gc(L, LUA_GCCOLLECT, 0);
    };
    token->runtime->addThreadCompletionHandler(L, std::move(cleanup));

    CurlMultiManager* manager = getCurlMultiManager(token->runtime, error);
    if (!manager)
    {
        token->fail("network request failed: " + error);
        return lua_yield(L, 0);
    }

    if (!manager->addRequest(std::move(state), error))
    {
        token->fail("network request failed: " + error);
        return lua_yield(L, 0);
    }

    return lua_yield(L, 0);
}

} // namespace net::client
