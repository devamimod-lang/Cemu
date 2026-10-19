#!/usr/bin/env python3
"""Extracts FSRCNN weights from a TensorFlow .pb without TensorFlow.

A minimal protobuf reader rather than a dependency, because installing
TensorFlow (and numpy, and protobuf) to read 39 KB of floats would be an
absurd trade - and it would put a build-time dependency on the user's machine
for something that runs once.

Only the parts of the schema that matter are decoded:
  GraphDef        { repeated NodeDef node = 1 }
  NodeDef         { string name = 1; string op = 2; map<string,AttrValue> attr = 5 }
  AttrValue       { TensorProto tensor = 8 }
  TensorProto     { int dtype = 1; TensorShapeProto shape = 2; bytes content = 4 }
  TensorShapeProto{ repeated Dim dim = 2 }  Dim { int64 size = 1 }

Weight layout, which is what the shader has to agree with: TensorFlow stores
conv kernels as [kernel_h, kernel_w, in_channels, out_channels], innermost
dimension last. Getting this order wrong produces a plausible-looking image
that is subtly wrong everywhere, so the script prints the shapes for checking
against the published architecture rather than asking anyone to trust it.
"""
import struct
import sys

WIRE_VARINT, WIRE_64, WIRE_LEN, WIRE_32 = 0, 1, 2, 5


def read_varint(buf, i):
    result, shift = 0, 0
    while True:
        b = buf[i]
        i += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, i
        shift += 7


def fields(buf):
    """Yields (field_number, wire_type, payload) for one message."""
    i = 0
    n = len(buf)
    while i < n:
        key, i = read_varint(buf, i)
        fnum, wtype = key >> 3, key & 7
        if wtype == WIRE_VARINT:
            val, i = read_varint(buf, i)
            yield fnum, wtype, val
        elif wtype == WIRE_LEN:
            ln, i = read_varint(buf, i)
            yield fnum, wtype, buf[i:i + ln]
            i += ln
        elif wtype == WIRE_32:
            yield fnum, wtype, buf[i:i + 4]
            i += 4
        elif wtype == WIRE_64:
            yield fnum, wtype, buf[i:i + 8]
            i += 8
        else:
            raise ValueError(f"unsupported wire type {wtype}")


def parse_tensor(buf):
    shape, content = [], b""
    for fnum, _, payload in fields(buf):
        if fnum == 2:                                   # tensor_shape
            for dfn, _, dim in fields(payload):
                if dfn == 2:                            # dim
                    for sfn, _, size in fields(dim):
                        if sfn == 1:
                            shape.append(size)
        elif fnum == 4:                                 # tensor_content
            content = payload
    floats = list(struct.unpack(f"<{len(content) // 4}f", content)) if content else []
    return shape, floats


def parse_graph(path):
    data = open(path, "rb").read()
    out = []
    for fnum, _, node in fields(data):
        if fnum != 1:
            continue
        name, op, tensor = None, None, None
        for nfn, _, payload in fields(node):
            if nfn == 1:
                name = payload.decode("utf-8", "replace")
            elif nfn == 2:
                op = payload.decode("utf-8", "replace")
            elif nfn == 5:                              # attr map entry
                key, val = None, None
                for mfn, _, mp in fields(payload):
                    if mfn == 1:
                        key = mp.decode("utf-8", "replace")
                    elif mfn == 2:
                        val = mp
                if key == "value" and val is not None:
                    for afn, _, ap in fields(val):
                        if afn == 8:                    # AttrValue.tensor
                            tensor = parse_tensor(ap)
        if op == "Const" and tensor and tensor[1]:
            out.append((name, tensor[0], tensor[1]))
    return out


if __name__ == "__main__":
    consts = parse_graph(sys.argv[1])
    print(f"{len(consts)} Const tensors with float data\n")
    total = 0
    for name, shape, vals in consts:
        total += len(vals)
        preview = ", ".join(f"{v:+.5f}" for v in vals[:3])
        print(f"{name:42s} shape={str(shape):20s} n={len(vals):6d}  [{preview} ...]")
    print(f"\ntotal parameters: {total}")
