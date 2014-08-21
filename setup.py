#! /usr/bin/env python

#from distutils.core import setup, Extension
from setuptools import setup, Extension

setup(
    name='cbor',
    version='0.1.10',
    description='RFC 7049 - Concise Binary Object Representation',
    long_description="""
An implementation of RFC 7049 - Concise Binary Object Representation (CBOR).

CBOR is comparable to JSON, has a superset of JSON's ability, but serializes to a binary format which is smaller and faster to generate and parse.

The two primary functions are cbor.loads() and cbor.dumps().

This library includes a C implementation which runs 3-5 times faster than the Python standard library's C-accelerated implementanion of JSON. This is also includes a 100% Python implementation.
""",
    author='Brian Olson',
    author_email='bolson@bolson.org',
    url='https://code.google.com/p/cbor',
    packages=['cbor'],
    package_dir={'cbor':'cbor'},
    ext_modules=[
        Extension(
            'cbor._cbor',
            include_dirs=['c/'],
            sources=['c/cbormodule.c'],
            headers=['c/cbor.h'],
        )
    ],
    license='AGPL',
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: GNU Affero General Public License v3',
        'Operating System :: OS Independent',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3.3',
        'Programming Language :: Python :: 3.4',
        'Programming Language :: C',
        'Topic :: Software Development :: Libraries :: Python Modules',
    ],
)
