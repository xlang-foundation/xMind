"""AgentFlow backend entry point, executed by the sibling xlang3 runtime."""

import argparse
import json
import os
from pathlib import Path
import httpx
import uvicorn
from agentflow.server import create_app
from agentflow.providers import provider_from_env


def main():
    parser = argparse.ArgumentParser(prog="agentflow")
    parser.add_argument("command", choices=["serve", "sessions", "session", "run", "events", "cancel", "approvals", "decide"])
    parser.add_argument("--database", default=".agentflow/state.sqlite")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--workspace", default=".")
    parser.add_argument("--allow-write", action="store_true")
    parser.add_argument("--ask-write", action="store_true")
    parser.add_argument("--approval")
    parser.add_argument("--decision", choices=["allow", "deny"])
    parser.add_argument("--url", default="http://127.0.0.1:8765")
    parser.add_argument("--session")
    parser.add_argument("--run")
    parser.add_argument("--prompt")
    parser.add_argument("--title", default="New session")
    parser.add_argument("--after", type=int, default=0)
    args = parser.parse_args()
    if args.command == "serve":
        database = Path(args.database).resolve()
        database.parent.mkdir(parents=True, exist_ok=True)
        servers = json.loads(os.environ.get("AGENTFLOW_MCP_SERVERS", "[]"))
        if not isinstance(servers, list):
            parser.error("AGENTFLOW_MCP_SERVERS must be a JSON list")
        if args.allow_write and args.ask_write:
            parser.error("Choose either --allow-write or --ask-write")
        uvicorn.run(create_app(database, provider_from_env(), args.workspace, args.allow_write, servers, args.ask_write),
                    host="127.0.0.1", port=args.port)
        return
    with httpx.Client(base_url=args.url, timeout=60) as client:
        if args.command == "sessions":
            response = client.get("/v1/sessions")
        elif args.command == "session":
            response = client.post("/v1/sessions", json={"title": args.title})
        elif args.command == "run":
            if not args.session or not args.prompt:
                parser.error("run requires --session and --prompt")
            response = client.post("/v1/runs", json={"session_id": args.session, "prompt": args.prompt})
        elif args.command == "decide":
            if not args.approval or not args.decision:
                parser.error("decide requires --approval and --decision")
            response = client.post("/v1/approvals/" + args.approval, json={"decision": args.decision})
        else:
            if not args.run:
                parser.error(args.command + " requires --run")
            if args.command == "cancel":
                response = client.post("/v1/runs/" + args.run + "/cancel")
            elif args.command == "approvals":
                response = client.get("/v1/runs/" + args.run + "/approvals")
            else:
                with client.stream("GET", "/v1/runs/" + args.run + "/stream",
                                   params={"after": args.after}, timeout=None) as stream:
                    stream.raise_for_status()
                    for line in stream.iter_lines():
                        if line.startswith("data: "):
                            print(line[6:], flush=True)
                return
        response.raise_for_status()
        print(json.dumps(response.json(), indent=2))


if __name__ == "__main__":
    main()
