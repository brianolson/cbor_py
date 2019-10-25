An implementation of [RFC 7049](https://tools.ietf.org/html/rfc7049) - Concise Binary Object Representation (CBOR) in Python, including a C-accelerated implementation.

[CBOR](https://cbor.io/) is comparable to JavaScript Object Notation (JSON); it has a superset of JSON’s ability, but serializes to a binary format which is smaller and faster to generate and parse. The `cbor` library provides convenient `loads()` and `dumps()` methods like the `json` standard library for JSON.

The C-accelerated implemenation runs 3-5 times faster than the Python standard library's C-accelerated implementation of JSON. The following compares the `cbor` library to the standard `json` library in Python 2.7:

```bash
serialized 50000 objects:
  into 1163097 cbor bytes in 0.05 seconds (1036613.48/s)
  and 1767001 json bytes in 0.22 seconds (224772.48/s)

compress to 999179 bytes cbor.gz and 1124500 bytes json.gz

load 50000 objects:
  from cbor in 0.07 secs (763708.80/sec)
  and json in 0.32 (155348.97/sec)
```

There is also a pure-python implementation which gets about 1/3 the speed of the standard `json` C-accelerated speed.

Tested in Python 2.7.5, 2,7.6, 3.3.3, 3.4.0, and 3.5.2

[Listed on PyPI](https://pypi.org/project/cbor/) the `cbor` library can be installed using:

```bash
pip install cbor
````

For an equivalent Go implementation, see https://github.com/brianolson/cbor_go
