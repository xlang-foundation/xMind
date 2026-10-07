from mcp import ClientSession
from mcp.server.fastmcp import FastMCP

server = FastMCP("AgentFlow compatibility probe")

@server.tool()
def add(a: int, b: int) -> int:
    """Add two integers."""
    return a + b

print("MCP SDK imported and tool registered")
