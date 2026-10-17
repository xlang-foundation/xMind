# xMind's bounded, private xlang3 tool worker protocol.
# The parent process has already bound the trusted module and function from
# startup configuration. Tool input and output cross IPC as JSON strings only.
import importlib
import hashlib
import hmac
import json
import time


class AgentWorker:
    def __init__(self, module_name, function_name, secret_hex):
        self.secret = bytes.fromhex(secret_hex)
        if len(self.secret) != 32:
            raise ValueError("worker capability must contain 32 bytes")
        self.last_sequence = 0
        self.module = importlib.import_module(module_name)
        self.function = getattr(self.module, function_name)
        if not callable(self.function):
            raise TypeError("trusted worker binding is not callable")

    def _authorize(self, sequence, signature, message):
        if type(sequence) is not int or sequence != self.last_sequence + 1:
            raise PermissionError("worker request sequence is invalid")
        if not isinstance(signature, str) or len(signature) != 64:
            raise PermissionError("worker request authentication is invalid")
        expected = hmac.new(self.secret, message, hashlib.sha256).hexdigest()
        if not hmac.compare_digest(signature, expected):
            raise PermissionError("worker request authentication is invalid")
        self.last_sequence = sequence

    def handshake(self, sequence, signature):
        message = str(sequence).encode("ascii") + b"\nxMind worker handshake v1"
        self._authorize(sequence, signature, message)
        return True

    def invoke(self, sequence, signature, arguments_json):
        if not isinstance(arguments_json, str) or len(arguments_json.encode("utf-8")) > 262144:
            raise ValueError("worker arguments exceed limit")
        message = str(sequence).encode("ascii") + b"\n" + arguments_json.encode("utf-8")
        self._authorize(sequence, signature, message)
        arguments = json.loads(arguments_json)
        if not isinstance(arguments, dict):
            raise ValueError("worker arguments must be an object")
        result = self.function(arguments)
        encoded = json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > 1048576:
            raise ValueError("worker result exceeds limit")
        return encoded


def start(port, module_name, function_name, secret_hex):
    worker = AgentWorker(module_name, function_name, secret_hex)
    register_remote_object("xmind_agent_worker", worker)
    lrpc_listen(port, False)
    while True:
        time.sleep(1)
