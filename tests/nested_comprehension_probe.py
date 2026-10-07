"""Minimal xlang3 closure-scope diagnostic. No native change or workaround applied."""

def probe():
    visited = {"ready"}
    nodes = {"next": {"depends_on": ["ready"]}}
    return [name for name, node in nodes.items()
            if all(dep in visited for dep in node.get("depends_on", []))]

assert probe() == ["next"]
print("nested comprehension closure passed")
