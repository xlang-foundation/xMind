"""Execute using xlang3 against an independent Node official SDK server."""

import asyncio
import sys
from agentflow.mcp_client import MCPClient


async def main():
    client = MCPClient(sys.argv[1])
    try:
        result = await client.initialize()
        assert result["serverInfo"]["name"] == "independent-node-peer"
        assert any(tool["name"] == "echo" for tool in await client.list_tools())
        result = await client.call_tool("echo", {"text": "xlang3 interoperability"})
        assert result["structuredContent"]["text"] == "xlang3 interoperability"
        assert not result.get("isError", False)
        print("xlang3 MCP client initialized, discovered and called official Node SDK peer")
    finally:
        await client.close()


asyncio.run(main())
