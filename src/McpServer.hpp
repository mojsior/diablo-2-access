#pragma once

namespace d2access {

// MCP (streamable HTTP, JSON responses) server on http://127.0.0.1:13450/mcp.
// Lets an MCP client read events and gameplay state and send mod keys.
void StartMcpServer();
void StopMcpServer();

} // namespace d2access
