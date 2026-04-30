local client = require("@self/client")
local server = require("@self/server")

local net = {}

--- HTTP request metadata, including optional method, body, and headers.
export type Metadata = client.Metadata
--- An HTTP response, containing status code, headers, body, and a success flag.
export type Response = client.Response
--- Options for establishing a WebSocket connection, including optional event handlers.
export type WebSocketOptions = client.WebSocketOptions
--- A client-side WebSocket connection handle.
export type WebSocket = client.WebSocket
--- An HTTP request received by the server, containing method, path, body, query params, and headers.
export type ReceivedRequest = server.ReceivedRequest
--- A response that can be returned from a server handler: a plain string body, or a table with status, body, and headers.
export type ServerResponse = server.ServerResponse
--- A request handler function: receives a `ReceivedRequest` and `Server`, and returns a `ServerResponse`.
export type Handler = server.Handler
--- Route params extracted from a route pattern.
export type RouteParams<Path> = server.RouteParams<Path>
--- A route handler request with `params` specialized from its route pattern.
export type RouteRequest<Path> = server.RouteRequest<Path>
--- A routes table checked against params extracted from route patterns.
export type CheckedRoutes<RouteTable = server.Routes> = server.CheckedRoutes<RouteTable>
--- Configuration for starting an HTTP server.
export type Configuration = server.Configuration
--- Route-aware configuration target for Luau's type-function inference.
export type CheckedConfiguration<RouteTable = server.Routes> = server.CheckedConfiguration<RouteTable>
--- A running HTTP server handle, with hostname, port, and lifecycle methods.
export type Server = server.Server
--- A server-side WebSocket connection handle.
export type ServerWebSocket = server.ServerWebSocket
--- Event handlers for server-side WebSocket connections.
export type WebSocketHandlers = server.WebSocketHandlers

net.client = client
net.server = server

return net
