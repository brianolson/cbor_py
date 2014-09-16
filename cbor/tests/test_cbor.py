#!python
# -*- coding: utf-8 -*-

import base64
import json
import random
import sys
import time
import unittest
import zlib


from cbor.cbor import dumps as pydumps
from cbor.cbor import loads as pyloads
from cbor.cbor import dump as pydump
from cbor.cbor import load as pyload
from cbor.cbor import Tag
from cbor._cbor import dumps as cdumps
from cbor._cbor import loads as cloads
from cbor._cbor import dump as cdump
from cbor._cbor import load as cload


_IS_PY3 = sys.version_info[0] >= 3


if _IS_PY3:
    _range = range
    from io import BytesIO as StringIO
else:
    _range = xrange
    from cStringIO import StringIO


class TestRoot(object):
    @classmethod
    def loads(cls, *args):
        return cls._ld[0](*args)
    @classmethod
    def dumps(cls, *args):
        return cls._ld[1](*args)
    @classmethod
    def speediterations(cls):
        return cls._ld[2]
    @classmethod
    def load(cls, *args):
        return cls._ld[3](*args)
    @classmethod
    def dump(cls, *args):
        return cls._ld[4](*args)

# Can't set class level function pointers, because then they expect a
# (cls) first argument. So, toss them in a list to hide them.
class TestPyPy(TestRoot):
    _ld = [pyloads, pydumps, 1000, pyload, pydump]

class TestPyC(TestRoot):
    _ld = [pyloads, cdumps, 2000, pyload, cdump]

class TestCPy(TestRoot):
    _ld = [cloads, pydumps, 2000, cload, pydump]

class TestCC(TestRoot):
    _ld = [cloads, cdumps, 150000, cload, cdump]


if _IS_PY3:
    def _join_jsers(jsers):
        return (''.join(jsers)).encode('utf8')
    def hexstr(bs):
        return ' '.join(map(lambda x: '{0:02x}'.format(x), bs))
else:
    def _join_jsers(jsers):
        return b''.join(jsers)
    def hexstr(bs):
        return ' '.join(map(lambda x: '{0:02x}'.format(ord(x)), bs))


class XTestCBOR(object):
    def _oso(self, ob):
        ser = self.dumps(ob)
        try:
            o2 = self.loads(ser)
            assert ob == o2, '%r != %r from %s' % (ob, o2, base64.b16encode(ser))
        except Exception as e:
            sys.stderr.write('failure on buf len={0} {1!r} ob={2!r} {3!r}; {4}\n'.format(len(ser), hexstr(ser), ob, ser, e))
            raise

    def _osos(self, ob):
        obs = self.dumps(ob)
        o2 = self.loads(obs)
        o2s = self.dumps(o2)
        assert obs == o2s

    def test_basic(self):
        self._oso(1)
        self._oso(0)
        self._oso(True)
        self._oso(False)
        self._oso(None)
        self._oso(-1)
        self._oso(-1.5)
        self._oso(1.5)
        self._oso(1000)
        self._oso(-1000)
        self._oso(1000000000)
        self._oso(-1000000000)
        self._oso(1000000000000000)
        self._oso(-1000000000000000)
        self._oso([])
        self._oso([1,2,3])
        self._oso({})
        self._oso(b'aoeu1234\x00\xff')
        self._oso(u'åöéûのかめ亀')
        self._oso(Tag(1234, 'aoeu'))

    def test_random_ints(self):
        icount = self.speediterations()
        for i in _range(icount):
            v = random.randint(-1000000000, 1000000000)
            self._oso(v)
        oldv = []
        for i in _range(int(icount / 10)):
            v = random.randint(-1000000000000000000000, 1000000000000000000000)
            self._oso(v)
            oldv.append(v)

    def test_randobs(self):
        icount = self.speediterations()
        for i in _range(icount):
            ob = _randob()
            self._oso(ob)

    def test_tuple(self):
        l = [1,2,3]
        t = tuple(l)
        ser = self.dumps(t)
        o2 = self.loads(ser)
        assert l == o2

    def test_speed_vs_json(self):
        # It should be noted that the python standard library has a C implementation of key parts of json encoding and decoding
        icount = self.speediterations()
        obs = [_randob_notag() for x in _range(icount)]
        st = time.time()
        bsers = [self.dumps(o) for o in obs]
        nt = time.time()
        cbor_ser_time = nt - st
        jsers = [json.dumps(o) for o in obs]
        jt = time.time()
        json_ser_time = jt - nt
        cbor_byte_count = sum(map(len, bsers))
        json_byte_count = sum(map(len, jsers))
        sys.stderr.write(
            'serialized {nobs} objects into {cb} cbor bytes in {ct:.2f} seconds ({cops:.2f}/s, {cbps:.1f}B/s) and {jb} json bytes in {jt:.2f} seconds ({jops:.2f}/s, {jbps:.1f}B/s)\n'.format(
            nobs=len(obs),
            cb=cbor_byte_count,
            ct=cbor_ser_time,
            cops=len(obs) / cbor_ser_time,
            cbps=cbor_byte_count / cbor_ser_time,
            jb=json_byte_count,
            jt=json_ser_time,
            jops=len(obs) / json_ser_time,
            jbps=json_byte_count / json_ser_time))
        bsersz = zlib.compress(b''.join(bsers))
        jsersz = zlib.compress(_join_jsers(jsers))
        sys.stderr.write('compress to {0} bytes cbor.gz and {1} bytes json.gz\n'.format(
            len(bsersz), len(jsersz)))

        st = time.time()
        bo2 = [self.loads(b) for b in bsers]
        bt = time.time()
        cbor_load_time = bt - st
        jo2 = [json.loads(b) for b in jsers]
        jt = time.time()
        json_load_time = jt - bt
        sys.stderr.write('load {nobs} objects from cbor in {ct:.2f} secs ({cops:.2f}/sec, {cbps:.1f}B/s) and json in {jt:.2f} ({jops:.2f}/sec, {jbps:.1f}B/s)\n'.format(
            nobs=len(obs),
            ct=cbor_load_time,
            cops=len(obs) / cbor_load_time,
            cbps=cbor_byte_count / cbor_load_time,
            jt=json_load_time,
            jops=len(obs) / json_load_time,
            jbps=json_byte_count / json_load_time
        ))

    def test_loads_none(self):
        try:
            ob = self.loads(None)
            assert False, "expected ValueError when passing in None"
        except ValueError:
            pass

    def test_concat(self):
        "Test that we can concatenate output and retrieve the objects back out."
        obs = ['aoeu', 2, {}, [1,2,3]]
        self._oso(obs)
        fob = StringIO()

        for ob in obs:
            self.dump(ob, fob)
        fob.seek(0)
        obs2 = []
        obs2.append(self.load(fob))
        obs2.append(self.load(fob))
        obs2.append(self.load(fob))
        obs2.append(self.load(fob))
        assert obs == obs2


class TestCBORPyPy(unittest.TestCase, XTestCBOR, TestPyPy):
    pass

class TestCBORCPy(unittest.TestCase, XTestCBOR, TestCPy):
    pass

class TestCBORPyC(unittest.TestCase, XTestCBOR, TestPyC):
    pass

class TestCBORCC(unittest.TestCase, XTestCBOR, TestCC):
    pass


def _randob():
    return _randob_x(_randob_probabilities, _randob_probsum, _randob)

def _randob_notag():
    return _randob_x(_randob_probabilities_notag, _randob_notag_probsum, _randob_notag)

def _randArray(randob=_randob):
    return [randob() for x in _range(random.randint(0,5))]

_chars = [chr(x) for x in _range(ord(' '), ord('~'))]

def _randString(randob=_randob):
    return ''.join([random.choice(_chars) for x in _range(random.randint(1,10))])


def _randDict(randob=_randob):
    ob = {}
    for x in _range(random.randint(0,5)):
        ob[_randString()] = randob()
    return ob


def _randTag(randob=_randob):
    t = Tag()
    # Tags 0..36 are know standard things we might implement special
    # decoding for. This number will grow over time, and this test
    # need to be adjusted to only assign unclaimed tags for Tag<->Tag
    # encode-decode testing.
    t.tag = random.randint(37, 1000000)
    t.value = randob()
    return t

def _randInt(randob=_randob):
    return random.randint(-1000000, 1000000)


_randob_probabilities = [
    (0.1, _randDict),
    (0.1, _randTag),
    (0.2, _randArray),
    (0.3, _randString),
    (0.4, _randInt),
]

_randob_probsum = sum([x[0] for x in _randob_probabilities])

_randob_probabilities_notag = [
    (0.1, _randDict),
    (0.2, _randArray),
    (0.3, _randString),
    (0.4, _randInt),
]

_randob_notag_probsum = sum([x[0] for x in _randob_probabilities_notag])

def _randob_x(probs=_randob_probabilities, probsum=_randob_probsum, randob=_randob):
    pos = random.uniform(0, probsum)
    for p, op in probs:
        if pos < p:
            return op(randob)
        pos -= p
    return None


if __name__ == '__main__':
  unittest.main()
