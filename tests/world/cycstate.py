"""Read cycle-host save states (.cycstate): their chunks and the NES RAM."""
import struct


def chunks(data):
    assert data[:8] == b"CYCSTATE" and struct.unpack_from("<I", data, 8)[0] in (2, 4)
    pos = 32
    while pos < len(data):
        tag, size = struct.unpack_from("<4sI", data, pos)
        pos += 8
        assert pos + size <= len(data)
        yield tag, pos, size
        pos += size
        if tag == b"END ":
            assert pos == len(data)
            return
    raise AssertionError("Missing END chunk")


def ram(data):
    pos, size = next((p, n) for t, p, n in chunks(data) if t == b"HW  ")
    assert size == 2072, "Unqualified hardware ABI"
    return data[pos + 23:pos + 23 + 2048]
