# xMind's bounded, private xlang3 tool worker protocol.
# The parent process has already bound the trusted module and function from
# startup configuration. Tool input and output cross IPC as JSON strings only.
import importlib
import json
import time


class AgentWorker:
    def __init__(self, module_name, function_name):
        self.module = importlib.import_module(module_name)
        self.function = getattr(self.module, function_name)
        if not callable(self.function):
            raise TypeError("trusted worker binding is not callable")

    def invoke(self, arguments_json):
        if not isinstance(arguments_json, str) or len(arguments_json.encode("utf-8")) > 262144:
            raise ValueError("worker arguments exceed limit")
        arguments = json.loads(arguments_json)
        if not isinstance(arguments, dict):
            raise ValueError("worker arguments must be an object")
        result = self.function(arguments)
        encoded = json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > 1048576:
            raise ValueError("worker result exceeds limit")
        return encoded


def start(port, module_name, function_name):
    worker = AgentWorker(module_name, function_name)
    register_remote_object("xmind_agent_worker", worker)
    lrpc_listen(port, False)
    while True:
        time.sleep(1)
