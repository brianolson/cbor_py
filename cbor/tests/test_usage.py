#!python
from __future__ import division  # / => float
import resource
import sys
import unittest

from .test_cbor import _randob


from cbor._cbor import dumps as cdumps
from cbor._cbor import loads as cloads
from cbor._cbor import dump as cdump
from cbor._cbor import load as cload


_TEST_COUNT = 100000
_TEST_OUTER = 3


_IS_PY3 = sys.version_info[0] >= 3


if _IS_PY3:
    _range = range
    from io import BytesIO as StringIO
else:
    _range = xrange
    from cStringIO import StringIO


class TestUsage(unittest.TestCase):
    def test_dumps_usage(self):
        '''
        repeatedly serialize, check that usage doesn't go up
        '''
        start_usage = resource.getrusage(resource.RUSAGE_SELF)
        usage_history = [start_usage]
        for o in _range(_TEST_OUTER):
            for i in _range(_TEST_COUNT):
                ob = _randob()
                blob = cdumps(ob)
                # and silently drop the result. I hope the garbage collector works!
            t_usage = resource.getrusage(resource.RUSAGE_SELF)
            usage_history.append(t_usage)
        end_usage = usage_history[-1]
        dmaxrss = end_usage.ru_maxrss - start_usage.ru_maxrss
        didrss = end_usage.ru_idrss - start_usage.ru_idrss
        dmaxrsspct = ((end_usage.ru_maxrss != 0) and (dmaxrss / end_usage.ru_maxrss)) or 0
        didrsspct = ((end_usage.ru_idrss != 0) and (didrss / end_usage.ru_idrss)) or 0

        sys.stderr.write('maxrss: {} - {}, d={} ({:.2f}%)\n'.format(start_usage.ru_maxrss, end_usage.ru_maxrss, dmaxrss, dmaxrsspct * 100.0))
        sys.stderr.write('idrss: {} - {}, d={} ({:.2f}%)\n'.format(start_usage.ru_idrss, end_usage.ru_idrss, didrss, didrsspct * 100.0))

        assert (dmaxrsspct) < 0.05, [x.ru_maxrss for x in usage_history]
        assert (didrsspct) < 0.05, [x.ru_idrss for x in usage_history]

    def test_loads_usage(self):
        '''
        repeatedly serialize, check that usage doesn't go up
        '''
        ## Just a string passes!
        #ob = 'sntaoheusnatoheusnaotehuasnoetuhaosentuhaoesnth'
        ## Just an array passes!
        #ob = [1,2,3,4,5,6,7,8,9,12,12,13]
        ## Just a dict passes!
        #ob = {'a':'b', 'c':'d', 'e':'f', 'g':'h'}
        # dict of dict is doom!
        #ob = {'a':{'b':'c', 'd':'e', 'f':'g'}, 'x':'p'}
        ob = {'aoeu':[1,2,3,4],'foo':'bar','pants':{'foo':0xb44, 'pi':3.14}, 'flubber': [{'x':'y', 'z':[None, 2, []]}, 2, 'hello']}
        blob = cdumps(ob)
        start_usage = resource.getrusage(resource.RUSAGE_SELF)
        usage_history = [start_usage]
        for o in _range(_TEST_OUTER):
            for i in _range(_TEST_COUNT):
                dob = cloads(blob)
                # and silently drop the result. I hope the garbage collector works!
            t_usage = resource.getrusage(resource.RUSAGE_SELF)
            usage_history.append(t_usage)
        end_usage = usage_history[-1]
        dmaxrss = end_usage.ru_maxrss - start_usage.ru_maxrss
        didrss = end_usage.ru_idrss - start_usage.ru_idrss
        dmaxrsspct = ((end_usage.ru_maxrss != 0) and (dmaxrss / end_usage.ru_maxrss)) or 0
        didrsspct = ((end_usage.ru_idrss != 0) and (didrss / end_usage.ru_idrss)) or 0

        sys.stderr.write('maxrss: {} - {}, d={} ({:.2f}%)\n'.format(start_usage.ru_maxrss, end_usage.ru_maxrss, dmaxrss, dmaxrsspct * 100.0))
        sys.stderr.write('idrss: {} - {}, d={} ({:.2f}%)\n'.format(start_usage.ru_idrss, end_usage.ru_idrss, didrss, didrsspct * 100.0))

        assert (dmaxrsspct) < 0.05, [x.ru_maxrss for x in usage_history]
        assert (didrsspct) < 0.05, [x.ru_idrss for x in usage_history]


if __name__ == '__main__':
    unittest.main()
