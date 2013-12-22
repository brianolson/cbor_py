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
from cbor import dumps
from cbor._cbor import loads


class TestCBOR(unittest.TestCase):
    def _oso(self, ob):
        ser = dumps(ob)
        o2 = loads(ser)
        assert ob == o2, '%r != %r from %s' % (ob, o2, base64.b16encode(ser))

    def _osos(self, ob):
        obs = dumps(ob)
        o2 = loads(obs)
        o2s = dumps(o2)
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
        self._oso({})
        self._oso(b'aoeu1234\x00\xff')
        self._oso(u'åöéûのかめ亀')

    def test_random_ints(self):
        for i in xrange(10000):
            v = random.randint(-1000000000, 1000000000)
            self._oso(v)
        oldv = []
        for i in xrange(1000):
            v = random.randint(-1000000000000000000000, 1000000000000000000000)
            self._oso(v)
            oldv.append(v)

    def test_randobs(self):
        for i in xrange(10000):
            ob = _randob()
            self._oso(ob)

    def test_speed_vs_json(self):
        # It should be noted that the python standard library has a C implementation of key parts of json encoding and decoding
        obs = [_randob() for x in xrange(10000)]
        st = time.time()
        bsers = [dumps(o) for o in obs]
        nt = time.time()
        cbor_ser_time = nt - st
        jsers = [json.dumps(o) for o in obs]
        jt = time.time()
        json_ser_time = jt - nt
        sys.stderr.write('serialized {0} objects into {1} cbor bytes in {2:.2f} seconds and {3} json bytes in {4:.2f} seconds\n'.format(
            len(obs),
            sum(map(len, bsers)), cbor_ser_time,
            sum(map(len, jsers)), json_ser_time))
        bsersz = zlib.compress(b''.join(bsers))
        jsersz = zlib.compress(b''.join(jsers))
        sys.stderr.write('compress to {0} bytes cbor.gz and {1} bytes json.gz\n'.format(
            len(bsersz), len(jsersz)))

        st = time.time()
        bo2 = [loads(b) for b in bsers]
        bt = time.time()
        cbor_load_time = bt - st
        jo2 = [json.loads(b) for b in jsers]
        jt = time.time()
        json_load_time = jt - bt
        sys.stderr.write('load {0} objects from cbor in {1:.2f} secs and json in {2:.2f}\n'.format(
            len(obs),
            cbor_load_time,
            json_load_time))

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
