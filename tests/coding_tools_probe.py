"""Search/edit workflow with ambiguity rejection, ignored paths and permissions."""

import asyncio
import tempfile
from pathlib import Path
from agentflow.tools import WorkspaceTools


async def main():
    with tempfile.TemporaryDirectory() as root:
        Path(root, "app.py").write_text("answer = 41\nprint(answer)\n", encoding="utf-8")
        Path(root, ".git").mkdir()
        Path(root, ".git", "secret").write_text("answer = secret", encoding="utf-8")
        tools = WorkspaceTools(root, allow_write=True)
        matches = await tools.execute("search_files", {"query": "answer ="})
        assert len(matches["matches"]) == 1
        assert matches["matches"][0]["line"] == 1
        result = await tools.execute("replace_text", {"path": "app.py", "old_text": "answer = 41", "new_text": "answer = 42"})
        assert result["before"].startswith("answer = 41")
        assert (await tools.execute("read_file", {"path": "app.py"}))["content"].startswith("answer = 42")
        before = Path(root, "app.py").read_text(encoding="utf-8")
        try:
            await tools.execute("replace_text", {"path": "app.py", "old_text": "answer", "new_text": "bad"})
            raise AssertionError("Ambiguous replacement accepted")
        except ValueError:
            pass
        assert Path(root, "app.py").read_text(encoding="utf-8") == before
        try:
            await WorkspaceTools(root).execute("replace_text", {"path": "app.py", "old_text": "42", "new_text": "43"})
            raise AssertionError("Edit without permission accepted")
        except PermissionError:
            pass
    print("coding search/edit, ambiguity rejection, ignored metadata and permission checks passed")


asyncio.run(main())
