import _ctypes


class Item(_ctypes._SimpleCData):
    _type_ = "i"


def verify():
    return 1 if issubclass(Item * 2, _ctypes.Array) else 0
