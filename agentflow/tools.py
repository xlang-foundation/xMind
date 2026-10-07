"""Workspace-bound coding tools with explicit mutation permissions."""

from pathlib import Path


class WorkspaceTools:
    def __init__(self, root, allow_write=False):
        self.root = Path(root).resolve(strict=True)
        self.allow_write = allow_write

    def schemas(self):
        tools = []
        for name, description, properties, required in [
            ("read_file", "Read a UTF-8 file within the workspace.", {"path": {"type": "string"}}, ["path"]),
            ("list_files", "List files in a workspace directory.", {"path": {"type": "string"}}, ["path"]),
            ("write_file", "Write a UTF-8 file; requires write permission.",
             {"path": {"type": "string"}, "content": {"type": "string"}}, ["path", "content"]),
            ("search_files", "Search workspace text files for a literal string, returning up to 100 matching lines.",
             {"query": {"type": "string"}}, ["query"]),
            ("replace_text", "Replace one exact unique text occurrence; requires write permission.",
             {"path": {"type": "string"}, "old_text": {"type": "string"}, "new_text": {"type": "string"}},
             ["path", "old_text", "new_text"]),
        ]:
            tools.append({"type": "function", "function": {"name": name, "description": description,
                          "parameters": {"type": "object", "properties": properties,
                                         "required": required, "additionalProperties": False}}})
        return tools

    def path(self, relative):
        if not isinstance(relative, str) or Path(relative).is_absolute():
            raise ValueError("Path must be relative to the workspace")
        target = (self.root / relative).resolve()
        if target != self.root and self.root not in target.parents:
            raise PermissionError("Path escapes workspace")
        return target

    def requires_approval(self, name):
        return name in ("write_file", "replace_text") and not self.allow_write

    async def execute_approved(self, name, arguments):
        # Grant exactly one operation through a fresh registry; never mutate
        # shared permission flags while concurrent runs are active.
        return await WorkspaceTools(self.root, allow_write=True).execute(name, arguments)

    async def execute(self, name, arguments):
        expected = {"read_file": {"path"}, "list_files": {"path"}, "write_file": {"path", "content"},
                    "search_files": {"query"}, "replace_text": {"path", "old_text", "new_text"}}
        if name not in expected:
            raise ValueError("Unknown tool: " + name)
        if not isinstance(arguments, dict) or set(arguments) != expected[name]:
            raise ValueError("Invalid tool arguments")
        if name == "search_files":
            query = arguments["query"]
            if not isinstance(query, str) or not query or len(query) > 4096:
                raise ValueError("Search query must be a nonempty string of at most 4096 characters")
            matches = []
            examined = 0
            excluded = {".git", ".venv", ".agentflow", "node_modules", "__pycache__"}
            for item in self.root.rglob("*"):
                relative = item.relative_to(self.root)
                if excluded.intersection(relative.parts) or item.is_symlink() or not item.is_file():
                    continue
                self.path(str(relative))
                examined += 1
                if examined > 10000:
                    return {"matches": matches, "truncated": True}
                if item.stat().st_size > 1024 * 1024:
                    continue
                try:
                    text = item.read_text(encoding="utf-8")
                except (UnicodeError, OSError):
                    continue
                for number, line in enumerate(text.splitlines(), 1):
                    if query in line:
                        matches.append({"path": str(relative), "line": number, "text": line[:4096]})
                        if len(matches) == 100:
                            return {"matches": matches, "truncated": True}
            return {"matches": matches, "truncated": False}
        target = self.path(arguments["path"])
        if name == "read_file":
            if target.stat().st_size > 1024 * 1024:
                raise ValueError("File exceeds 1 MiB tool limit")
            return {"path": arguments["path"], "content": target.read_text(encoding="utf-8")}
        if name == "list_files":
            return {"files": sorted(item.name for item in target.iterdir())[:1000]}
        if not self.allow_write:
            raise PermissionError(name + " requires explicit workspace write permission")
        if name == "replace_text":
            old = arguments["old_text"]
            new = arguments["new_text"]
            if not isinstance(old, str) or not old or not isinstance(new, str):
                raise ValueError("Replacement requires nonempty old_text and string new_text")
            if target.stat().st_size > 1024 * 1024:
                raise ValueError("File exceeds 1 MiB tool limit")
            before = target.read_text(encoding="utf-8")
            if before.count(old) != 1:
                raise ValueError("old_text must match exactly once; inspect the file before editing")
            content = before.replace(old, new, 1)
        else:
            content = arguments["content"]
        if not isinstance(content, str) or len(content.encode("utf-8")) > 1024 * 1024:
            raise ValueError("Invalid or oversized file content")
        if target.exists() and target.stat().st_size > 1024 * 1024:
            raise ValueError("Existing file exceeds 1 MiB tool limit")
        before = target.read_text(encoding="utf-8") if target.exists() else None
        target.write_text(content, encoding="utf-8")
        return {"path": arguments["path"], "before": before, "after": content}
