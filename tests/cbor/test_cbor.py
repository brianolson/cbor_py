#!python
# -*- coding: utf-8 -*-

import unittest

from cbor import dumps, loads


class TestCBOR(unittest.TestCase):
    def _oso(self, ob):
        o2 = loads(dumps(ob))
        self.assertEqual(ob, o2)

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


if __name__ == '__main__':
  unittest.main()
