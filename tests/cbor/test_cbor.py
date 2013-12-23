#!python
# -*- coding: utf-8 -*-

import base64
import json
import random
import sys
import time
import unittest
import zlib

# TODO: make this test file parameterized on testing {python,c}{loads,dumps} in all four combinations

#from cbor import dumps, loads
from cbor import dumps as pydumps
from cbor import loads as pyloads
from cbor._cbor import dumps as cdumps
from cbor._cbor import loads as cloads


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

class TestPyPy(TestRoot):
    _ld = [pyloads, pydumps, 1000]

class TestPyC(TestRoot):
    _ld = [pyloads, cdumps, 2000]

class TestCPy(TestRoot):
    _ld = [cloads, pydumps, 2000]

class TestCC(TestRoot):
    _ld = [cloads, cdumps, 50000]


class XTestCBOR(object):
    def _oso(self, ob):
        ser = self.dumps(ob)
        o2 = self.loads(ser)
        assert ob == o2, '%r != %r from %s' % (ob, o2, base64.b16encode(ser))

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

    def test_random_ints(self):
        icount = self.speediterations()
        for i in xrange(icount):
            v = random.randint(-1000000000, 1000000000)
            self._oso(v)
        oldv = []
        for i in xrange(int(icount / 10)):
            v = random.randint(-1000000000000000000000, 1000000000000000000000)
            self._oso(v)
            oldv.append(v)

    def test_randobs(self):
        icount = self.speediterations()
        for i in xrange(icount):
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
        obs = [_randob() for x in xrange(icount)]
        st = time.time()
        bsers = [self.dumps(o) for o in obs]
        nt = time.time()
        cbor_ser_time = nt - st
        jsers = [json.dumps(o) for o in obs]
        jt = time.time()
        json_ser_time = jt - nt
        sys.stderr.write('serialized {0} objects into {1} cbor bytes in {2:.2f} seconds ({3:.2f}/s) and {4} json bytes in {5:.2f} seconds ({6:.2f}/s)\n'.format(
            len(obs),
            sum(map(len, bsers)), cbor_ser_time, len(obs) / cbor_ser_time,
            sum(map(len, jsers)), json_ser_time, len(obs) / json_ser_time))
        bsersz = zlib.compress(b''.join(bsers))
        jsersz = zlib.compress(b''.join(jsers))
        sys.stderr.write('compress to {0} bytes cbor.gz and {1} bytes json.gz\n'.format(
            len(bsersz), len(jsersz)))

        st = time.time()
        bo2 = [self.loads(b) for b in bsers]
        bt = time.time()
        cbor_load_time = bt - st
        jo2 = [json.loads(b) for b in jsers]
        jt = time.time()
        json_load_time = jt - bt
        sys.stderr.write('load {0} objects from cbor in {1:.2f} secs ({2:.2f}/sec) and json in {3:.2f} ({4:.2f}/sec)\n'.format(
            len(obs),
            cbor_load_time, len(obs) / cbor_load_time,
            json_load_time, len(obs) / json_load_time))

    def test_loads_none(self):
        try:
            ob = self.loads(None)
            assert False, "expected ValueError when passing in None"
        except ValueError, ve:
            pass

    # def test_concat():
    #     "Test that we can concatenate output and retrieve the objects back out."
    #     obs = ['aoeu', 2, {}, [1,2,3]]
    #     _ob_ser_ob(obs)
    #     sers = map(dumps, obs)
    #     sercat = b''.join(sers)
    #     fob = StringIO(sercat)
    #     obs2 = []
    #     obs2.append(load(fob))
    #     obs2.append(load(fob))
    #     obs2.append(load(fob))
    #     obs2.append(load(fob))
    #     assert obs == obs2


class TestCBORPyPy(unittest.TestCase, XTestCBOR, TestPyPy):
    pass

class TestCBORCPy(unittest.TestCase, XTestCBOR, TestCPy):
    pass

class TestCBORPyC(unittest.TestCase, XTestCBOR, TestPyC):
    pass

class TestCBORCC(unittest.TestCase, XTestCBOR, TestCC):
    pass


def _randArray():
    return [_randob() for x in xrange(random.randint(0,5))]

_chars = [chr(x) for x in xrange(ord(' '), ord('~'))]

def _randString():
    return ''.join([random.choice(_chars) for x in xrange(random.randint(1,10))])


def _randDict():
    ob = {}
    for x in xrange(random.randint(0,5)):
        ob[_randString()] = _randob()
    return ob


def _randInt():
    return random.randint(-1000000, 1000000)


_randob_probabilities = [
    (0.1, _randDict),
    (0.2, _randArray),
    (0.3, _randString),
    (0.4, _randInt),
]

_randob_probsum = sum(map(lambda x: x[0], _randob_probabilities))


def _randob():
    pos = random.uniform(0, _randob_probsum)
    for p, op in _randob_probabilities:
        if pos < p:
            return op()
        pos -= p
    return None


if __name__ == '__main__':
  unittest.main()
