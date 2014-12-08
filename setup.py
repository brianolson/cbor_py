#! /usr/bin/env python
# Copyright 2014 Brian Olson
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#from distutils.core import setup, Extension
from setuptools import setup, Extension

setup(
    name='cbor',
    version='0.1.14',
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
    license='Apache',
    classifiers=[
	'Development Status :: 4 - Beta',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: Apache Software License',
        'Operating System :: OS Independent',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3.3',
        'Programming Language :: Python :: 3.4',
        'Programming Language :: C',
        'Topic :: Software Development :: Libraries :: Python Modules',
    ],
)
