def transform(arguments):
    return {"result": "worker:" + arguments["text"], "count": arguments["count"] + 1}


def slow(arguments):
    import time
    time.sleep(5)
    return {"done": True}
