#!python

try:
    # try C library _cbor.so
    from ._cbor import loads, dumps
except:
    # fall back to 100% python implementation
    from .cbor import loads, dumps


__all__ = ['loads', 'dumps']
