-- lute-lint-global-ignore(unused_variable)
local server = {}

type function _RouteParams(Path)
	local params = types.newtable()

	if not Path:is("singleton") then
		return params
	end

	local path = Path:value()
	if typeof(path) ~= "string" then
		return params
	end

	local index = 1
	while index <= #path do
		local slash = string.find(path, "/", index, true) or (#path + 1)
		local segment = string.sub(path, index, slash - 1)

		if string.sub(segment, 1, 1) == ":" and #segment > 1 then
			params:setproperty(types.singleton(string.sub(segment, 2)), types.string)
		elseif segment == "*" then
			params:setproperty(types.singleton("*"), types.string)
		end

		index = slash + 1
	end

	return params
end

type function _RouteRequest(Path)
	return types.newtable({
		[types.singleton("method")] = types.string,
		[types.singleton("path")] = types.string,
		[types.singleton("body")] = types.string,
		[types.singleton("query")] = types.newtable(nil, { index = types.string, readresult = types.string, writeresult = types.string }),
		[types.singleton("headers")] = types.newtable(nil, { index = types.string, readresult = types.string, writeresult = types.string }),
		[types.singleton("params")] = _RouteParams(Path),
	})
end

type function _CheckedRoutes(RouteTable, ServerTy, ResponseTy)
	local routes = types.newtable()

	if not RouteTable:is("table") then
		return routes
	end

	local responseOrNil = types.optional(ResponseTy)

	for pathTy, _prop in RouteTable:properties() do
		local request = _RouteRequest(pathTy)
		local handler = types.newfunction({ head = { request, ServerTy } }, { head = { responseOrNil } })
		local routeValue = types.unionof(handler, ResponseTy)
		local methodRoutes = types.newtable()

		methodRoutes:setproperty(types.singleton("GET"), types.optional(routeValue))
		methodRoutes:setproperty(types.singleton("POST"), types.optional(routeValue))
		methodRoutes:setproperty(types.singleton("PUT"), types.optional(routeValue))
		methodRoutes:setproperty(types.singleton("DELETE"), types.optional(routeValue))
		methodRoutes:setproperty(types.singleton("PATCH"), types.optional(routeValue))
		methodRoutes:setproperty(types.singleton("HEAD"), types.optional(routeValue))
		methodRoutes:setproperty(types.singleton("OPTIONS"), types.optional(routeValue))

		routes:setproperty(pathTy, types.unionof(routeValue, methodRoutes))
	end

	return routes
end

--- An HTTP request received by the server, containing method, path, body, query params, and headers.
export type ReceivedRequest = {
	method: string,
	path: string,
	body: string,
	query: { [string]: string },
	headers: { [string]: string },
	params: { [string]: string },
}

--- A response that can be returned from a server handler: a plain string body, or a table with status, body, and headers.
export type ServerResponse = string | {
	status: number?,
	body: string?,
	headers: { [string]: string }?,
}

--- A running HTTP server handle, with hostname, port, and lifecycle methods.
export type Server = {
	hostname: string,
	port: number,
	close: () -> (),
	upgrade: (self: Server, req: ReceivedRequest) -> boolean,
}

--- A request handler function: receives a `ReceivedRequest` and `Server`, and returns a `ServerResponse`.
export type Handler = (request: ReceivedRequest, server: Server) -> ServerResponse?

--- Route params extracted from a route pattern. For example, `/users/:id`
--- produces `{ id: string }`, and `/assets/*` produces `{ ["*"]: string }`.
export type RouteParams<Path> = _RouteParams<Path>

--- A route handler request with `params` specialized from its route pattern.
export type RouteRequest<Path> = _RouteRequest<Path>

export type CheckedRouteHandler<Path> = (request: RouteRequest<Path>, server: Server) -> ServerResponse?

export type CheckedRouteValue<Path> = CheckedRouteHandler<Path> | ServerResponse

export type CheckedMethodRoutes<Path> = {
	GET: CheckedRouteValue<Path>?,
	POST: CheckedRouteValue<Path>?,
	PUT: CheckedRouteValue<Path>?,
	DELETE: CheckedRouteValue<Path>?,
	PATCH: CheckedRouteValue<Path>?,
	HEAD: CheckedRouteValue<Path>?,
	OPTIONS: CheckedRouteValue<Path>?,
}

export type CheckedRouteEntry<Path> = CheckedRouteValue<Path> | CheckedMethodRoutes<Path>

export type RouteHandler = Handler | ServerResponse

export type MethodRoutes = {
	GET: RouteHandler?,
	POST: RouteHandler?,
	PUT: RouteHandler?,
	DELETE: RouteHandler?,
	PATCH: RouteHandler?,
	HEAD: RouteHandler?,
	OPTIONS: RouteHandler?,
	CONNECT: RouteHandler?,
	TRACE: RouteHandler?,
}

export type RouteEntry = RouteHandler | MethodRoutes

--- A routes table mapping URL patterns to handlers or responses. Patterns are matched
--- in this order: exact path, parameter path (`/users/:id`), wildcard path (`/assets/*`),
--- then global catch-all (`/*`). Unmatched requests fall back to the top-level `handler`.
export type Routes = { [string]: RouteEntry }

--- A routes table that checks each route entry against the params extracted from
--- its route pattern. This is currently the intended target for Luau's table
--- literal inference when used through `serve({ routes = ... })`.
export type CheckedRoutes<RouteTable = Routes> = RouteTable & _CheckedRoutes<RouteTable, Server, ServerResponse>

--- A server-side WebSocket connection handle.
export type ServerWebSocket = {
	send: (self: ServerWebSocket, data: string | buffer) -> number,
	close: (self: ServerWebSocket, code: number?, message: string?) -> (),
}

--- Event handlers for server-side WebSocket connections.
export type WebSocketHandlers = {
	open: ((ws: ServerWebSocket) -> ())?,
	message: ((ws: ServerWebSocket, message: string | buffer) -> ())?,
	close: ((ws: ServerWebSocket, code: number, message: string) -> ())?,
	drain: ((ws: ServerWebSocket) -> ())?,
}

--- Configuration for starting an HTTP server.
export type Configuration = {
	hostname: string?,
	port: number?,
	reuseport: boolean?,
	tls: { certfilename: string, keyfilename: string, passphrase: string?, cafilename: string? }?,
	handler: Handler?,
	routes: Routes?,
	websocket: WebSocketHandlers?,
}

--- Route-aware configuration target for Luau's type-function inference.
export type CheckedConfiguration<RouteTable = Routes> = {
	hostname: string?,
	port: number?,
	reuseport: boolean?,
	tls: { certfilename: string, keyfilename: string, passphrase: string?, cafilename: string? }?,
	handler: Handler?,
	routes: CheckedRoutes<RouteTable>?,
	websocket: WebSocketHandlers?,
}

--- Starts an HTTP server with the given handler or full configuration. Returns a `Server` handle.
function server.serve(config: Handler | Configuration): Server
	error("not implemented")
end

return server
