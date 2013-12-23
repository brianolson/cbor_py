#!python

try:
    from ._cbor import loads, dumps
except:
    from .cbor import loads, dumps


__all__ = ['loads', 'dumps']
