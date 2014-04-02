#!python

try:
    # try C library _cbor.so
    from ._cbor import loads, dumps, load, dump
except:
    # fall back to 100% python implementation
    from .cbor import loads, dumps, load, dump


__all__ = ['loads', 'dumps', 'load', 'dump']
