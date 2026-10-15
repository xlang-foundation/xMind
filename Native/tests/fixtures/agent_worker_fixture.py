calls = 0
active = 0
max_active = 0


def transform(arguments):
    return {"result": "worker:" + arguments["text"], "count": arguments["count"] + 1}


def counter(arguments):
    global calls, active, max_active
    active += 1
    max_active = max(max_active, active)
    delay_ms = arguments.get("delay_ms", 0)
    if delay_ms:
        import time
        time.sleep(delay_ms / 1000)
    calls += 1
    active -= 1
    return {"calls": calls, "max_active": max_active, "value": arguments.get("value", "")}


def slow(arguments):
    import time
    time.sleep(5)
    return {"done": True}
