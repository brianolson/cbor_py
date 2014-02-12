#!/usr/bin/env python

"""
Test CBOR implementation against common "test vectors" set from
https://github.com/cbor/test-vectors/
"""

import base64
import json
import logging
import os
import sys


_IS_PY3 = sys.version_info[0] >= 3


from cbor.cbor import dumps as pydumps
from cbor.cbor import loads as pyloads
from cbor._cbor import dumps as cdumps
from cbor._cbor import loads as cloads


def test_vectors():
    here = os.path.dirname(__file__)
    jf = os.path.abspath(os.path.join(here, '../../../test-vectors/appendix_a.json'))
    if not os.path.exists(jf):
        logging.warn('cannot find test-vectors/appendix_a.json, tried: %r', jf)
        return

    if _IS_PY3:
        tv = json.load(open(jf, 'r'))
    else:
        tv = json.load(open(jf, 'rb'))
    anyerr = False
    for row in tv:
        if 'decoded' in row:
            decoded = row['decoded']
            cbdata = base64.b64decode(row['cbor'])
            cb = cloads(cbdata)
            if cb != decoded:
                anyerr = True
                sys.stderr.write('expected {0!r} got {1!r} c failed to decode cbor {2}\n'.format(decoded, cb, base64.b16encode(cbdata)))

            cb = pyloads(cbdata)
            if cb != decoded:
                anyerr = True
                sys.stderr.write('expected {0!r} got {1!r} py failed to decode cbor {2}\n'.format(decoded, cb, base64.b16encode(cbdata)))



    assert not anyerr
