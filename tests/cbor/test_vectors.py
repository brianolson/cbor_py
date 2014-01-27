#!/usr/bin/env python

"""
Test CBOR implementation against common "test vectors" set from
https://github.com/cbor/test-vectors/
"""

import base64
import cbor
import json
import logging
import os
import sys


_IS_PY3 = sys.version_info[0] >= 3


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
            cb = cbor.loads(cbdata)
            if cb != decoded:
                anyerr = True
                sys.stderr.write('expected {0!r} got {1!r} failed to decode cbor {2}\n'.format(decoded, cb, base64.b16encode(cbdata)))

    assert not anyerr
