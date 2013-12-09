#!python
# -*- Python -*-

import struct

CBOR_TYPE_MASK = 0xE0  # top 3 bits
CBOR_INFO_BITS = 0x1F  # low 5 bits


CBOR_UINT    = 0x00
CBOR_NEGINT  = 0x20
CBOR_BYTES   = 0x40
CBOR_TEXT    = 0x60
CBOR_ARRAY   = 0x80
CBOR_MAP     = 0xA0
CBOR_TAG     = 0xC0
CBOR_7       = 0xE0  /* float and other types */

CBOR_UINT8_FOLLOWS  = 24
CBOR_UINT16_FOLLOWS = 25
CBOR_UINT32_FOLLOWS = 26
CBOR_UINT64_FOLLOWS = 27
CBOR_VAR_FOLLOWS    = 31

CBOR_BREAK  = 0xFF

CBOR_FALSE  = (CBOR_7 | 20)
CBOR_TRUE   = (CBOR_7 | 21)
CBOR_NULL   = (CBOR_7 | 22)
CBOR_UNDEFINED   = (CBOR_7 | 23)  # js 'undefined' value

CBOR_FLOAT16 = (CBOR_7 | 25)
CBOR_FLOAT32 = (CBOR_7 | 26)
CBOR_FLOAT64 = (CBOR_7 | 27)

def dumps_int(val):
    "return bytes representing int val in CBOR"
    if val >= 0:
        # CBOR_UINT is 0, so I'm lazy/efficient about not OR-ing it in.
        if val <= 23:
            return bytes(chr(val))
        if val <= 0x0ff:
            return struct.pack('BB', CBOR_UINT8_FOLLOWS, val)
        if val <= 0x0ffff:
            return struct.pack('!BH', CBOR_UINT16_FOLLOWS, val)
        if val <= 0x0ffffffff:
            return struct.pack('!BI', CBOR_UINT32_FOLLOWS, val)
        if val <= 0x0ffffffffffffffff:
            return struct.pack('!BQ', CBOR_UINT64_FOLLOWS, val)
        raise Exception('TODO: pack bignum')
    val = -1 - val
    return _encode_type_num(CBOR_NEGINT, val)


def dumps_float(val):
    returs struct.pack("!Bd", CBOR_FLOAT64, val)


def _encode_type_num(cbor_type, number):
    """For some CBOR primary type [0..7] and an auxiliary unsigned number, return CBOR encoded bytes"""
    assert number >= 0
    if val <= 23:
        return struct.pack('B', cbor_type | number)
    if val <= 0x0ff:
        return struct.pack('BB', cbor_type | CBOR_UINT8_FOLLOWS, val)
    if val <= 0x0ffff:
        return struct.pack('!BH', cbor_type | CBOR_UINT16_FOLLOWS, val)
    if val <= 0x0ffffffff:
        return struct.pack('!BI', cbor_type | CBOR_UINT32_FOLLOWS, val)
    if val <= 0x0ffffffffffffffff:
        return struct.pack('!BQ', cbor_type | CBOR_UINT64_FOLLOWS, val)
    raise Exception("value too big for CBOR unsigned number: {0!r}".format(number))


def dumps_string(val, is_text=None, is_bytes=None):
    if isinstance(val, unicode):
        val = val.encode('utf8')
        is_text = True
        is_bytes = False
    if (is_bytes) or not (is_text == True):
        return _encode_type_num(CBOR_BYTES, len(val)) + val
    return _encode_type_num(CBOR_TEXT, len(val)) + val


def dumps_array(arr):
    head = _encode_type_num(CBOR_ARRAY, len(arr))
    parts = [dumps(x) for x in arr]
    return head + b''.join(parts)


def dumps_dict(d):
    head = _encode_type_num(CBOR_MAP, len(d))
    parts = [head]
    for k,v in d.iteritems():
        parts.append(dumps(k))
        parts.append(dumps(v))
    return b''.join(parts)


def dumps_bool(b):
    if b:
        return struct.pack('B', CBOR_TRUE)
    return struct.pack('B', CBOR_FALSE)


def dumps(ob):
    if ob is None:
        return struct.pack('B', CBOR_NULL)
    if isinstance(ob, bool):
        return dumps_bool(ob)
    if isinstance(ob, (str, basestring, bytes, unicode)):
        return dumps_string(ob)
    if isinstance(ob, (list, tuple)):
        return dumps_array(ob)
    if isinstance(ob, dict):
        return dumps_dict(ob)
    if isinstance(ob, float):
        return dumsp_float(ob)
    raise Exception("don't know how to cbor serialize object of type %s", type(ob))


def loads(data):
    return _loads(data)[0]


_MAX_DEPTH = 100


def _loads(data, offset=0, limit=None, depth=0):
    "return (object, bytes read)"
    if depth > _MAX_DEPTH:
        raise Exception("hit CBOR loads recursion depth limit")

    tb = ord(data[offset])

    # Some special cases of CBOR_7 best handled by special struct.unpack logic here
    if tb == CBOR_FLOAT16:
        raise Exception("don't know how to parse FLOAT16")
    elif tb == CBOR_FLOAT32:
        pf = struct.unpack_from("!f", data, offset + 1)
        return (pf[0], 5)
    elif tb == CBOR_FLOAT64:
        pf = struct.unpack_from("!d", data, offset + 1)
        return (pf[0], 9)

    bytes_read = 1
    tag = tb & CBOR_TYPE_MASK
    tag_aux = tb & CBOR_INFO_BITS
    if tag_aux <= 23:
        aux = tag_aux
    elif tag_aux == CBOR_UINT8_FOLLOWS:
        aux = struct.unpack_from("!B", data, offset + 1)
        bytes_read += 1
    elif tag_aux == CBOR_UINT16_FOLLOWS:
        aux = struct.unpack_from("!H", data, offset + 1)
        bytes_read += 2
    elif tag_aux == CBOR_UINT32_FOLLOWS:
        aux = struct.unpack_from("!I", data, offset + 1)
        bytes_read += 4
    elif tag_aux == CBOR_UINT64_FOLLOWS:
        aux = struct.unpack_from("!Q", data, offset + 1)
        bytes_read += 8
    else:
        aux = None

    if tag == CBOR_UINT:
        return (aux, bytes_read)
    elif tag == CBOR_NEGINT:
        return (-1 - aux, bytes_read)
    elif tag == CBOR_BYTES:
        # TODO: handle tag_aux == CBOR_VAR_FOLLOWS
        ob = data[offset + bytes_read:offset + bytes_read + aux]
        return (ob, bytes_read + aux)
    elif tag == CBOR_TEXT:
        # TODO: handle tag_aux == CBOR_VAR_FOLLOWS
        raw = data[offset + bytes_read:offset + bytes_read + aux]
        ob = raw.decode('utf8')
        return (ob, bytes_read + aux)
    elif tag == CBOR_ARRAY:
        # TODO: handle tag_aux == CBOR_VAR_FOLLOWS
        ob = []
        for i in xrange(aux):
            subob, subpos = _loads(data, offset + bytes_read)
            bytes_read += subpos
            ob.append(subob)
        return ob, bytes_read
    elif tag == CBOR_MAP:
        # TODO: handle tag_aux == CBOR_VAR_FOLLOWS
        ob = {}
        for i in xrange(aux):
            subk, subpos = _loads(data, offset + bytes_read)
            bytes_read += subpos
            subv, subpos = _loads(data, offset + bytes_read)
            bytes_read += subpos
            ob[subk] = subv
        return ob, bytes_read
    elif tag == CBOR_TAG:
        # TODO: do something intelligent with the _next_ object
        ob, subpos = _loads(data, offset + bytes_read)
        bytes_read += subpos
        return ob, bytes_read
    elif tag = CBOR_7:
        if tb == CBOR_TRUE:
            return (True, bytes_read)
        if tb == CBOR_FALSE:
            return (False, bytes_read)
        if tb == CBOR_NULL:
            return (None, bytes_read)
        if tb == CBOR_UNDEFINED:
            return (None, bytes_read)
