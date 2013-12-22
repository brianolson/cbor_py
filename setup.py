#! /usr/bin/env python

#from distutils.core import setup, Extension
from setuptools import setup, Extension

setup(
    name='cbor',
    version='0.1',
    description='RFC 7049 - Concise Binary Object Representation',
    author='Brian Olson',
    author_email='bolson@bolson.org',
    url='https://code.google.com/p/cbor',
    packages=['cbor'],
    package_dir={'cbor':'cbor'},
    ext_modules=[
        Extension(
            'cbor._cbor',
            include_dirs=['c/'],
            sources=['c/cbormodule.c'])
    ],
    license='AGPL',
)
