#!python

try:
    import cbor._cbor as cborlib
    from .cborfast import loads, dumps
except:
    from .cbor import loads, dumps


__all__ = ['loads', 'dumps']
