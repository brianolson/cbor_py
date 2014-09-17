try:
    # try C library _cbor.so
    from ._cbor import loads, dumps, load, dump
except:
    # fall back to 100% python implementation
    from .cbor import loads, dumps, load, dump

from .cbor import Tag


class ClassTag(object):
    '''
    For some CBOR tag_number, encode/decode Python class_type.
    class_type manily used for isintance(foo, class_type)
    Call encode_function() taking a Python instance and returning CBOR primitive types.
    Call decode_function() on CBOR primitive types and return an instance of the Python class_type (a factory function).
    '''
    def __init__(self, tag_number, class_type, encode_function, decode_function):
        self.tag_number = tag_number
        self.class_type = class_type
        self.encode_function = encode_function
        self.decode_function = decode_function


# TODO: This would be more efficient if it moved into cbor.py and
# cbormodule.c, happening inline so that there is only one traversal
# of the objects. But that would require two implementations. When
# this API has been used more and can be considered settled I should
# do that. -- Brian Olson 20140917_172229
class TagMapper(object):
    '''
    Translate Python objects and CBOR tagged data.
    Use the CBOR TAG system to note that some data is of a certain class.
    Dump while translating Python objects into a CBOR compatible representation.
    Load and translate CBOR primitives back into Python objects.
    '''
    def __init__(self, class_tags=None, raise_on_unknown_tag=False):
        '''
        class_tags: list of ClassTag objects
        '''
        self.class_tags = class_tags
        self.raise_on_unknown_tag = raise_on_unknown_tag

    def encode(self, obj):
        for ct in self.class_tags:
            if isinstance(obj, ct.class_type):
                return Tag(ct.tag_number, ct.encode_function(obj))
        if isinstance(obj, (list, tuple)):
            return [self.encode(x) for x in obj]
        if isinstance(obj, dict):
            # assume key is a primitive
            return {k:self.encode(v) for k,v in obj.iteritems()}
        # fall through, let underlying cbor.dump decide if it can encode object
        return obj

    def decode(self, obj):
        if isinstance(obj, Tag):
            for ct in self.class_tags:
                if ct.tag_number == obj.tag:
                    return ct.decode_function(obj.value)
            # unknown Tag
            if self.raise_on_unknown_tag:
                raise UnknownTagException(str(obj.tag))
            # otherwise, pass it through
            return obj
        if isinstance(obj, list):
            # update in place. cbor only decodes to list, not tuple
            for i,v in enumerate(obj):
                obj[i] = self.decode(v)
            return obj
        if isinstance(obj, dict):
            # update in place
            for k,v in obj.iteritems():
                # assume key is a primitive
                obj[k] = self.decode(v)
            return obj
        # non-recursive object (num,bool,blob,string)
        return obj

    def dump(self, obj, fp):
        dump(self.encode(obj), fp)

    def dumps(self, obj):
        return dumps(self.encode(obj))

    def load(self, fp):
        return self.decode(load(fp))

    def loads(self, blob):
        return self.decode(loads(blob))


class UnknownTagException(BaseException):
    pass
