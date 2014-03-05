#include "Python.h"

#include "cbor.h"

#include <math.h>
#include <stdint.h>

//#include <stdio.h>
#include <arpa/inet.h>



#ifdef Py_InitModule
// Python 2.7

#define HAS_FILE_READER 1
#define IS_PY3 0

#else

#define HAS_FILE_READER 0
#define IS_PY3 1

#endif

// Hey Look! It's a polymorphic object structure in C!

// read(, len): read len bytes and return in buffer, or NULL on error
// read1(, uint8_t*): read one byte and return 0 on success
// return_buffer(, *): release result of read(, len)
// delete(): destructor. free thiz and contents.
#define READER_FUNCTIONS \
    void* (*read)(void* self, Py_ssize_t len); \
    int (*read1)(void* self, uint8_t* oneByte); \
    void (*return_buffer)(void* self, void* buffer); \
    void (*delete)(void* self);

#define SET_READER_FUNCTIONS(thiz, clazz) (thiz)->read = clazz##_read;\
    (thiz)->read1 = clazz##_read1;\
    (thiz)->return_buffer = clazz##_return_buffer;\
    (thiz)->delete = clazz##_delete;

typedef struct _Reader {
    READER_FUNCTIONS;
} Reader;

static Reader* NewBufferReader(PyObject* ob);
static Reader* NewObjectReader(PyObject* ob);
#if HAS_FILE_READER
static Reader* NewFileReader(PyObject* ob);
#endif


static PyObject* loads_tag(Reader* rin, uint64_t aux);
static int loads_kv(PyObject* out, Reader* rin);

typedef struct VarBufferPart {
    void* start;
    uint64_t len;
    struct VarBufferPart* next;
} VarBufferPart;


// TODO: portably work this out at compile time
static int _is_big_endian = 0;

static int is_big_endian() {
    uint32_t val = 1234;
    _is_big_endian = val == htonl(val);
    //fprintf(stderr, "is_big_endian=%d\n", _is_big_endian);
    return _is_big_endian;
}


PyObject* decodeFloat16(Reader* rin) {
    // float16 parsing adapted from example code in spec
    uint8_t hibyte, lobyte;// = raw[pos];
    int err;
    int exp;
    int mant;
    double val;

    err = rin->read1(rin, &hibyte);
    if (err) { fprintf(stderr, "fail in float16[0]\n"); return NULL; }
    err = rin->read1(rin, &lobyte);
    if (err) { fprintf(stderr, "fail in float16[1]\n"); return NULL; }

    exp = (hibyte >> 2) & 0x1f;
    mant = ((hibyte & 0x3) << 8) | lobyte;
    if (exp == 0) {
	val = ldexp(mant, -24);
    } else if (exp != 31) {
	val = ldexp(mant + 1024, exp - 25);
    } else {
	val = mant == 0 ? INFINITY : NAN;
    }
    if (hibyte & 0x80) {
	val = -val;
    }
    return PyFloat_FromDouble(val);
}
PyObject* decodeFloat32(Reader* rin) {
    float val;
    uint8_t* raw = rin->read(rin, 4);
    if (!raw) { return NULL; }
    if (_is_big_endian) {
	// easy!
	val = *((float*)raw);
    } else {
	uint8_t* dest = (uint8_t*)(&val);
	dest[3] = raw[0];
	dest[2] = raw[1];
	dest[1] = raw[2];
	dest[0] = raw[3];
    }
    rin->return_buffer(rin, raw);
    return PyFloat_FromDouble(val);
}
PyObject* decodeFloat64(Reader* rin) {
    int si;
    uint64_t aux = 0;
    uint8_t* raw = rin->read(rin, 8);
    if (!raw) { return NULL; }
    for (si = 0; si < 8; si++) {
	aux = aux << 8;
	aux |= raw[si];
    }
    rin->return_buffer(rin, raw);
    return PyFloat_FromDouble(*((double*)(&aux)));
}

// parse following int value into *auxP
// return 0 on success, -1 on fail
static int handle_info_bits(Reader* rin, uint8_t cbor_info, uint64_t* auxP) {
    uint64_t aux;

    if (cbor_info <= 23) {
	// literal value <=23
	aux = cbor_info;
    } else if (cbor_info == CBOR_UINT8_FOLLOWS) {
	uint8_t taux;
	if (rin->read1(rin, &taux)) { fprintf(stderr, "fail in uint8\n"); return -1; }
	aux = taux;
    } else if (cbor_info == CBOR_UINT16_FOLLOWS) {
	uint8_t hibyte, lobyte;
	if (rin->read1(rin, &hibyte)) { fprintf(stderr, "fail in uint16[0]\n"); return -1; }
	if (rin->read1(rin, &lobyte)) { fprintf(stderr, "fail in uint16[1]\n"); return -1; }
	aux = (hibyte << 8) | lobyte;
    } else if (cbor_info == CBOR_UINT32_FOLLOWS) {
	uint8_t* raw = (uint8_t*)rin->read(rin, 4);
	if (!raw) { return -1; }
	aux = 
	    (raw[0] << 24) |
	    (raw[1] << 16) |
	    (raw[2] <<  8) |
	    raw[3];
	rin->return_buffer(rin, raw);
    } else if (cbor_info == CBOR_UINT64_FOLLOWS) {
        int si;
	uint8_t* raw = (uint8_t*)rin->read(rin, 8);
	if (!raw) { return -1; }
	aux = 0;
	for (si = 0; si < 8; si++) {
	    aux = aux << 8;
	    aux |= raw[si];
	}
	rin->return_buffer(rin, raw);
    } else {
	aux = 0;
    }
    *auxP = aux;
    return 0;
}

static PyObject* inner_loads_c(Reader* rin, uint8_t c);

static PyObject* inner_loads(Reader* rin) {
    uint8_t c;
    int err;

    err = rin->read1(rin, &c);
    if (err) { fprintf(stderr, "fail in loads tag\n"); return NULL; }
    return inner_loads_c(rin, c);
}

PyObject* inner_loads_c(Reader* rin, uint8_t c) {
    uint8_t cbor_type;
    uint8_t cbor_info;
    uint64_t aux;

    cbor_type = c & CBOR_TYPE_MASK;
    cbor_info = c & CBOR_INFO_BITS;

#if 0
    if (pos > len) {
	PyErr_SetString(PyExc_ValueError, "misparse, token went longer than buffer");
	return NULL;
    }

    pos += 1;
#endif

    if (cbor_type == CBOR_7) {
	if (cbor_info == CBOR_UINT16_FOLLOWS) { // float16
	    return decodeFloat16(rin);
	} else if (cbor_info == CBOR_UINT32_FOLLOWS) { // float32
	    return decodeFloat32(rin);
	} else if (cbor_info == CBOR_UINT64_FOLLOWS) {  // float64
	    return decodeFloat64(rin);
	}
	// not a float, fall through to other CBOR_7 interpretations
    }
    if (handle_info_bits(rin, cbor_info, &aux)) { return NULL; }

    PyObject* out = NULL;
    switch (cbor_type) {
    case CBOR_UINT:
	out = PyLong_FromUnsignedLongLong(aux);
	break;
    case CBOR_NEGINT:
	if (aux > 0x7fffffffffffffff) {
	    PyObject* bignum = PyLong_FromUnsignedLongLong(aux);
	    PyObject* minusOne = PyLong_FromLong(-1);
	    out = PyNumber_Subtract(minusOne, bignum);
	    Py_DECREF(minusOne);
	    Py_DECREF(bignum);
	} else {
	    out = PyLong_FromLongLong((long long)(((long long)-1) - aux));
	}
	break;
    case CBOR_BYTES:
	if (cbor_info == CBOR_VAR_FOLLOWS) {
	    size_t total = 0;
	    VarBufferPart* parts = NULL;
	    VarBufferPart* parts_tail = NULL;
	    uint8_t sc;
	    if (rin->read1(rin, &sc)) { fprintf(stderr, "r1 fail in var bytes tag\n"); return NULL; }
	    while (sc != CBOR_BREAK) {
		uint8_t scbor_type = sc & CBOR_TYPE_MASK;
		uint8_t scbor_info = sc & CBOR_INFO_BITS;
		uint64_t saux;
		void* blob;

		if (scbor_type != CBOR_BYTES) {
		    PyErr_Format(PyExc_ValueError, "expected subordinate BYTES block under VAR BYTES, but got %x", scbor_type);
		    return NULL;
		}
		if(handle_info_bits(rin, scbor_info, &saux)) { return NULL; }
		blob = rin->read(rin, saux);
		if (!blob) { return NULL; }
		if (parts_tail == NULL) {
		    parts = parts_tail = (VarBufferPart*)PyMem_Malloc(sizeof(VarBufferPart));
		} else {
		    parts_tail->next = (VarBufferPart*)PyMem_Malloc(sizeof(VarBufferPart));
		    parts_tail = parts_tail->next;
		}
		parts_tail->next = NULL;
		parts_tail->start = blob;
		parts_tail->len = saux;
		total += saux;
		if (rin->read1(rin, &sc)) { fprintf(stderr, "r1 fail in var bytes tag\n"); return NULL; }
	    }
	    // Done
	    {
		uint8_t* allbytes = (uint8_t*)PyMem_Malloc(total);
		uintptr_t op = 0;
		while (parts != NULL) {
		    VarBufferPart* next;
		    memcpy(allbytes + op, parts->start, parts->len);
		    op += parts->len;
		    next = parts->next;
		    PyMem_Free(parts);
		    parts = next;
		}
		out = PyBytes_FromStringAndSize((char*)allbytes, total);
		PyMem_Free(allbytes);
	    }
	} else {
	    void* raw = rin->read(rin, aux);
	    if (!raw) { return NULL; }
	    out = PyBytes_FromStringAndSize(raw, (Py_ssize_t)aux);
				  }
	break;
    case CBOR_TEXT:
	if (cbor_info == CBOR_VAR_FOLLOWS) {
	    PyObject* parts = PyList_New(0);
	    PyObject* joiner = PyUnicode_FromString("");
	    uint8_t sc;
	    if (rin->read1(rin, &sc)) { fprintf(stderr, "r1 fail in var text tag\n"); return NULL; }
	    while (sc != CBOR_BREAK) {
		PyObject* subitem = inner_loads_c(rin, sc);
		if (subitem == NULL) { fprintf(stderr, "fail in var text subitem\n"); return NULL; }
		PyList_Append(parts, subitem);
		if (rin->read1(rin, &sc)) { fprintf(stderr, "r1 fail in var text tag\n"); return NULL; }
	    }
	    // Done
	    out = PyUnicode_Join(joiner, parts);
	    Py_DECREF(joiner);
	    Py_DECREF(parts);
	} else {
	    void* raw = rin->read(rin, aux);
	    out = PyUnicode_FromStringAndSize((char*)raw, (Py_ssize_t)aux);
	}
	break;
    case CBOR_ARRAY:
	if (cbor_info == CBOR_VAR_FOLLOWS) {
	    uint8_t sc;
	    out = PyList_New(0);
	    if (rin->read1(rin, &sc)) { fprintf(stderr, "r1 fail in var array tag\n"); return NULL; }
	    while (sc != CBOR_BREAK) {
		PyObject* subitem = inner_loads_c(rin, sc);
		if (subitem == NULL) { fprintf(stderr, "fail in var array subitem\n"); return NULL; }
		PyList_Append(out, subitem);
		if (rin->read1(rin, &sc)) { fprintf(stderr, "r1 fail in var array tag\n"); return NULL; }
	    }
	    // Done
	} else {
            unsigned int i;
	    out = PyList_New((Py_ssize_t)aux);
	    for (i = 0; i < aux; i++) {
		PyObject* subitem = inner_loads(rin);
		if (subitem == NULL) { return NULL; }
		PyList_SetItem(out, (Py_ssize_t)i, subitem);
	    }
	}
	break;
    case CBOR_MAP:
	out = PyDict_New();
	if (cbor_info == CBOR_VAR_FOLLOWS) {
	    uint8_t sc;
	    if (rin->read1(rin, &sc)) { fprintf(stderr, "r1 fail in var map tag\n"); return NULL; }
	    while (sc != CBOR_BREAK) {
		PyObject* key = inner_loads_c(rin, sc);
		PyObject* value;
		if (key == NULL) { return NULL; }
		value = inner_loads(rin);
		if (value == NULL) { return NULL; }
		PyDict_SetItem(out, key, value);

		if (rin->read1(rin, &sc)) { fprintf(stderr, "r1 fail in var map tag\n"); return NULL; }
	    }
	} else {
            unsigned int i;
	    for (i = 0; i < aux; i++) {
		if (loads_kv(out, rin) != 0) {
		    return NULL;
		}
	    }
	}
	break;
    case CBOR_TAG:
	out = loads_tag(rin, aux);
	break;
    case CBOR_7:
	if (aux == 20) {
	    out = Py_False;
	    Py_INCREF(out);
	} else if (aux == 21) {
	    out = Py_True;
	    Py_INCREF(out);
	} else if (aux == 22) {
	    out = Py_None;
	    Py_INCREF(out);
	}
	break;
    default:
	break;
	// raise Exception("bogus cbor tag: %x")
    }
    return out;
}

static int loads_kv(PyObject* out, Reader* rin) {
    PyObject* key = inner_loads(rin);
    PyObject* value;
    if (key == NULL) { return -1; }
    value = inner_loads(rin);
    if (value == NULL) { return -1; }
    PyDict_SetItem(out, key, value);
    return 0;
}

static PyObject* loads_bignum(Reader* rin, uint8_t c) {
    PyObject* out = NULL;

    uint8_t bytes_info = c & CBOR_INFO_BITS;
    if (bytes_info < 24) {
        int i;
	PyObject* eight = PyLong_FromLong(8);
	out = PyLong_FromLong(0);
	for (i = 0; i < bytes_info; i++) {
	    // TODO: is this leaking like crazy?
	    PyObject* curbyte;
	    PyObject* tout = PyNumber_Lshift(out, eight);
	    Py_DECREF(out);
	    out = tout;
	    uint8_t cb;
	    if (rin->read1(rin, &cb)) { fprintf(stderr, "r1 fail in bignum %d/%d\n", i, bytes_info); return NULL; }
	    curbyte = PyLong_FromLong(cb);
	    tout = PyNumber_Or(out, curbyte);
	    Py_DECREF(curbyte);
	    Py_DECREF(out);
	    out = tout;
	}
	return out;
    } else {
	PyErr_Format(PyExc_NotImplementedError, "TODO: TAG BIGNUM for bigger bignum bytes_info=%d, len(ull)=%lu\n", bytes_info, sizeof(unsigned long long));
	return NULL;
    }
    return NULL;
}


static PyObject* loads_tag(Reader* rin, uint64_t aux) {
    PyObject* out = NULL;
    // return an object CBORTag(tagnum, nextob)
    if (aux == CBOR_TAG_BIGNUM) {
	// If the next object is bytes, interpret it here without making a PyObject for it.
	uint8_t sc;
	if (rin->read1(rin, &sc)) { fprintf(stderr, "r1 fail in bignum tag\n"); return NULL; }
	if ((sc & CBOR_TYPE_MASK) == CBOR_BYTES) {
	    return loads_bignum(rin, sc);
	} else {
	    PyErr_Format(PyExc_ValueError, "TAG BIGNUM not followed by bytes but %02x", sc);
	    return NULL;
	}
	PyErr_Format(PyExc_ValueError, "TODO: WRITEME CBOR TAG BIGNUM %02x ...\n", sc);
	return NULL;
    } else if (aux == CBOR_TAG_NEGBIGNUM) {
	// If the next object is bytes, interpret it here without making a PyObject for it.
	uint8_t sc;
	if (rin->read1(rin, &sc)) { fprintf(stderr, "r1 fail in negbignum tag\n"); return NULL; }
	if ((sc & CBOR_TYPE_MASK) == CBOR_BYTES) {
	    out = loads_bignum(rin, sc);
	    if (out != NULL) {
		PyObject* minusOne = PyLong_FromLong(-1);
		PyObject* tout = PyNumber_Subtract(minusOne, out);
		Py_DECREF(minusOne);
		Py_DECREF(out);
		out = tout;
		return out;
	    }
	} else {
	    PyErr_Format(PyExc_ValueError, "TAG NEGBIGNUM not followed by bytes but %02x", sc);
	    return NULL;
	}
	PyErr_Format(PyExc_ValueError, "TODO: WRITEME CBOR TAG NEGBIGNUM %02x ...\n", sc);
	return NULL;
    }
    out = inner_loads(rin);
    if (out == NULL) { return NULL; }
    {
	PyObject* cbor_module = PyImport_ImportModule("cbor");
	PyObject* moddict = PyModule_GetDict(cbor_module);
	PyObject* tag_class = PyDict_GetItemString(moddict, "Tag");
	PyObject* args = Py_BuildValue("(K,o)", aux, out);
	//PyObject* tout = PyInstance_New(tag_class, args, Py_None);
	PyObject* tout = PyType_GenericNew((PyTypeObject*)tag_class, args, Py_None);
	Py_DECREF(out);
	Py_DECREF(args);
	Py_DECREF(tag_class);
	// moddict is a 'borrowed reference'
	Py_DECREF(cbor_module);
	out = tout;
    }
    return out;
}


static PyObject*
cbor_loads(PyObject* noself, PyObject* args) {
    PyObject* ob;
    is_big_endian();
    if (PyType_IsSubtype(Py_TYPE(args), &PyList_Type)) {
	ob = PyList_GetItem(args, 0);
    } else if (PyType_IsSubtype(Py_TYPE(args), &PyTuple_Type)) {
	ob = PyTuple_GetItem(args, 0);
    } else {
	PyErr_Format(PyExc_ValueError, "args not list or tuple: %R\n", args);
	return NULL;
    }

    if (ob == Py_None) {
	PyErr_SetString(PyExc_ValueError, "got None for buffer to decode in loads");
	return NULL;
    }

#if 1
    {
	Reader* r = NewBufferReader(ob);
	if (!r) {
	    return NULL;
	}
	return inner_loads(r);
    }
#else
    {
	uint8_t* raw = (uint8_t*)PyBytes_AsString(ob);
	Py_ssize_t len = PyBytes_Size(ob);
	uintptr_t pos = 0;
	if (len == 0) {
	    PyErr_SetString(PyExc_ValueError, "got zero length string in loads");
	    return NULL;
	}
	if (raw == NULL) {
	    PyErr_SetString(PyExc_ValueError, "got NULL buffer for string");
	    return NULL;
	}
	return inner_loads(raw, &pos, len);
    }
#endif
}


#if HAS_FILE_READER

typedef struct _FileReader {
    READER_FUNCTIONS;
    FILE* fin;
    void* dst;
    Py_ssize_t dst_size;
} FileReader;

// read from a python builtin file which contains a C FILE*
static void* FileReader_read(void* self, Py_ssize_t len) {
    FileReader* thiz = (FileReader*)self;
    Py_ssize_t rtotal = 0;
    uintptr_t opos;
    if (len > thiz->dst_size) {
	thiz->dst = PyMem_Realloc(thiz->dst, len);
	thiz->dst_size = len;
    } else if ((thiz->dst_size > (128 * 1024)) && (len < 4096)) {
	PyMem_Free(thiz->dst);
	thiz->dst = PyMem_Malloc(len);
	thiz->dst_size = len;
    }
    opos = (uintptr_t)(thiz->dst);
    while (1) {
	size_t rlen = fread((void*)opos, 1, len, thiz->fin);
	if (rlen == 0) {
	    // file isn't going to give any more
	    PyErr_Format(PyExc_ValueError, "only got %zd bytes with %zd stil to read from file", rtotal, len);
	    PyMem_Free(thiz->dst);
	    thiz->dst = NULL;
	    return NULL;
	}
	rtotal += rlen;
	opos += rlen;
	len -= rlen;
	if (rtotal >= len) {
	    return thiz->dst;
	}
    }
}
static int FileReader_read1(void* self, uint8_t* oneByte) {
    FileReader* thiz = (FileReader*)self;
    return 1 == fread((void*)oneByte, 1, 1, thiz->fin);
}
static void FileReader_return_buffer(void* self, void* buffer) {
    // Nothing to do, we hold onto the buffer and maybe reuse it for next read
}
static void FileReader_delete(void* self) {
    FileReader* thiz = (FileReader*)self;
    if (thiz->dst) {
	PyMem_Free(thiz->dst);
    }
    PyMem_Free(thiz);
}
static Reader* NewFileReader(PyObject* ob) {
    FileReader* fr = (FileReader*)PyMem_Malloc(sizeof(FileReader));
    assert(PyFile_Check(ob));
    fr->fin = PyFile_AsFile(ob);
    fr->dst = NULL;
    fr->dst_size = 0;
    SET_READER_FUNCTIONS(fr, FileReader);
    return (Reader*)fr;
}

#endif /* Python 2.7 FileReader */


typedef struct _ObjectReader {
    READER_FUNCTIONS;
    PyObject* ob;

    // We got one object with all the bytes neccessary, and need to
    // DECREF it later.
    PyObject* retval;
    void* bytes;

    // OR, we got several objects, we DECREFed them as we went, and
    // need to Free() this buffer at the end.
    void* dst;
} ObjectReader;

// read from a python file-like object which has a .read(n) method
static void* ObjectReader_read(void* context, Py_ssize_t len) {
    ObjectReader* thiz = (ObjectReader*)context;
    Py_ssize_t rtotal = 0;
    uintptr_t opos = 0;
    while (rtotal < len) {
	PyObject* retval = PyObject_CallMethod(thiz->ob, "read", "n", len - rtotal, NULL);
	Py_ssize_t rlen;
	if (!PyBytes_Check(retval)) {
	    PyErr_SetString(PyExc_ValueError, "expected ob.read() to return a bytes object\n");
	    return NULL;
	}
	rlen = PyBytes_Size(retval);
	if (rlen > len - rtotal) {
	    fprintf(stderr, "TODO: raise exception: WAT ob.read() returned %ld bytes but only wanted %lu\n", rlen, len - rtotal);
	    return thiz->dst;
	}
	if (rlen == len) {
	    // best case! All in one call to read()
	    // We _keep_ a reference to retval until later.
	    thiz->retval = retval;
	    thiz->bytes = PyBytes_AsString(retval);
	    thiz->dst = NULL;
	    opos = 0;
	    return thiz->bytes;
	}
	if (thiz->dst == NULL) {
	    thiz->dst = PyMem_Malloc(len);
	    opos = (uintptr_t)thiz->dst;
	}
	// else, not enough all in one go
	memcpy((void*)opos, PyBytes_AsString(retval), rlen);
	Py_DECREF(retval);
	opos += rlen;
	rtotal += rlen;
    }
    return thiz->dst;
}
static int ObjectReader_read1(void* self, uint8_t* oneByte) {
    ObjectReader* thiz = (ObjectReader*)self;
    PyObject* retval = PyObject_CallMethod(thiz->ob, "read", "i", 1, NULL);
    Py_ssize_t rlen;
    if (retval == NULL) {
	fprintf(stderr, "call ob read(1) failed\n");
	return -1;
    }
    if (!PyBytes_Check(retval)) {
	PyErr_SetString(PyExc_ValueError, "expected ob.read() to return a bytes object\n");
	return -1;
    }
    rlen = PyBytes_Size(retval);
    if (rlen > 1) {
	PyErr_Format(PyExc_ValueError, "TODO: raise exception: WAT ob.read() returned %ld bytes but only wanted 1\n", rlen);
	return -1;
    }
    if (rlen == 1) {
	*oneByte = PyBytes_AsString(retval)[0];
	Py_DECREF(retval);
	return 0;
    }
    PyErr_SetString(PyExc_ValueError, "got nothing reading 1");
    return -1;
}
static void ObjectReader_return_buffer(void* context, void* buffer) {
    ObjectReader* thiz = (ObjectReader*)context;
    if (buffer == thiz->bytes) {
	Py_DECREF(thiz->retval);
	thiz->retval = NULL;
	thiz->bytes = NULL;
    } else if (buffer == thiz->dst) {
	PyMem_Free(thiz->dst);
	thiz->dst = NULL;
    } else {
	fprintf(stderr, "TODO: raise exception, could not release buffer %p, wanted dst=%p or bytes=%p\n", buffer, thiz->dst, thiz->bytes);
    }
}
static void ObjectReader_delete(void* context) {
    ObjectReader* thiz = (ObjectReader*)context;
    if (thiz->retval != NULL) {
	Py_DECREF(thiz->retval);
    }
    if (thiz->dst != NULL) {
	PyMem_Free(thiz->dst);
    }
    PyMem_Free(thiz);
}
static Reader* NewObjectReader(PyObject* ob) {
    ObjectReader* r = (ObjectReader*)PyMem_Malloc(sizeof(ObjectReader));
    r->ob = ob;
    r->retval = NULL;
    r->bytes = NULL;
    r->dst = NULL;
    SET_READER_FUNCTIONS(r, ObjectReader);
    return (Reader*)r;
}

typedef struct _BufferReader {
    READER_FUNCTIONS;
    uint8_t* raw;
    Py_ssize_t len;
    uintptr_t pos;
} BufferReader;

// read from a buffer, aka loads()
static void* BufferReader_read(void* context, Py_ssize_t len) {
    BufferReader* thiz = (BufferReader*)context;
    //fprintf(stderr, "br %p %d (%d)\n", thiz, len, thiz->len);
    if (len <= thiz->len) {
	void* out = (void*)thiz->pos;
	thiz->pos += len;
	thiz->len -= len;
	return out;
    }
    PyErr_Format(PyExc_ValueError, "buffer read for %zd but only have %zd\n", len, thiz->len);
    return NULL;
}
static int BufferReader_read1(void* self, uint8_t* oneByte) {
    BufferReader* thiz = (BufferReader*)self;
    //fprintf(stderr, "br %p _1_ (%d)\n", thiz, thiz->len);
    if (thiz->len <= 0) {
	PyErr_SetString(PyExc_LookupError, "buffer exhausted");
	return -1;
    }
    *oneByte = *((uint8_t*)thiz->pos);
    thiz->pos += 1;
    thiz->len -= 1;
    return 0;
}
static void BufferReader_return_buffer(void* context, void* buffer) {
    // nothing to do
}
static void BufferReader_delete(void* context) {
    BufferReader* thiz = (BufferReader*)context;
    PyMem_Free(thiz);
}
static Reader* NewBufferReader(PyObject* ob) {
    BufferReader* r = (BufferReader*)PyMem_Malloc(sizeof(BufferReader));
    SET_READER_FUNCTIONS(r, BufferReader);
    r->raw = (uint8_t*)PyBytes_AsString(ob);
    r->len = PyBytes_Size(ob);
    r->pos = (uintptr_t)r->raw;
    if (r->len == 0) {
	PyErr_SetString(PyExc_ValueError, "got zero length string in loads");
	return NULL;
    }
    if (r->raw == NULL) {
	PyErr_SetString(PyExc_ValueError, "got NULL buffer for string");
	return NULL;
    }
    //fprintf(stderr, "NBR(%llx, %ld)\n", r->pos, r->len);
    return (Reader*)r;
}


static PyObject*
cbor_load(PyObject* noself, PyObject* args) {
    PyObject* ob;
    Reader* reader;
    is_big_endian();
    if (PyType_IsSubtype(Py_TYPE(args), &PyList_Type)) {
	ob = PyList_GetItem(args, 0);
    } else if (PyType_IsSubtype(Py_TYPE(args), &PyTuple_Type)) {
	ob = PyTuple_GetItem(args, 0);
    } else {
	PyErr_Format(PyExc_ValueError, "args not list or tuple: %R\n", args);
	return NULL;
    }

    if (ob == Py_None) {
	PyErr_SetString(PyExc_ValueError, "got None for buffer to decode in loads");
	return NULL;
    }
#if HAS_FILE_READER
    if (PyFile_Check(ob)) {
	reader = NewFileReader(ob);
    } else
#endif
    {
	reader = NewObjectReader(ob);
    }
    return inner_loads(reader);
    //    PyErr_SetString(PyExc_NotImplementedError, "TODO: implement C load()");
    // return NULL;
}


static void tag_u64_out(uint8_t cbor_type, uint64_t aux, uint8_t* out, uintptr_t* posp) {
    uintptr_t pos = *posp;
    if (out != NULL) {
	out[pos] = cbor_type | CBOR_UINT64_FOLLOWS;
	out[pos+1] = (aux >> 56) & 0x0ff;
	out[pos+2] = (aux >> 48) & 0x0ff;
	out[pos+3] = (aux >> 40) & 0x0ff;
	out[pos+4] = (aux >> 32) & 0x0ff;
	out[pos+5] = (aux >> 24) & 0x0ff;
	out[pos+6] = (aux >> 16) & 0x0ff;
	out[pos+7] = (aux >>  8) & 0x0ff;
	out[pos+8] = aux & 0x0ff;
    }
    pos += 9;
    *posp = pos;
}


static void tag_aux_out(uint8_t cbor_type, uint64_t aux, uint8_t* out, uintptr_t* posp) {
    uintptr_t pos = *posp;
    if (aux <= 23) {
	// tiny literal
	if (out != NULL) {
	    out[pos] = cbor_type | aux;
	}
	pos += 1;
    } else if (aux <= 0x0ff) {
	// one byte value
	if (out != NULL) {
	    out[pos] = cbor_type | CBOR_UINT8_FOLLOWS;
	    out[pos+1] = aux;
	}
	pos += 2;
    } else if (aux <= 0x0ffff) {
	// two byte value
	if (out != NULL) {
	    out[pos] = cbor_type | CBOR_UINT16_FOLLOWS;
	    out[pos+1] = (aux >> 8) & 0x0ff;
	    out[pos+2] = aux & 0x0ff;
	}
	pos += 3;
    } else if (aux <= 0x0ffffffffL) {
	// four byte value
	if (out != NULL) {
	    out[pos] = cbor_type | CBOR_UINT32_FOLLOWS;
	    out[pos+1] = (aux >> 24) & 0x0ff;
	    out[pos+2] = (aux >> 16) & 0x0ff;
	    out[pos+3] = (aux >>  8) & 0x0ff;
	    out[pos+4] = aux & 0x0ff;
	}
	pos += 5;
    } else {
	// eight byte value
	tag_u64_out(cbor_type, aux, out, posp);
	return;
    }
    *posp = pos;
    return;
}

static int inner_dumps(PyObject* ob, uint8_t* out, uintptr_t* posp);

static int dumps_dict(PyObject* ob, uint8_t* out, uintptr_t* posp) {
    uintptr_t pos = *posp;
    Py_ssize_t dictiter = 0;
    PyObject* key;
    PyObject* val;
    Py_ssize_t dictlen = PyDict_Size(ob);
    int err;
    tag_aux_out(CBOR_MAP, dictlen, out, &pos);
    while (PyDict_Next(ob, &dictiter, &key, &val)) {
	err = inner_dumps(key, out, &pos);
	if (err != 0) { return err; }
	err = inner_dumps(val, out, &pos);
	if (err != 0) { return err; }
    }
    *posp = pos;
    return 0;
}


static void dumps_bignum(uint8_t tag, PyObject* val, uint8_t* out, uintptr_t* posp) {
    uintptr_t pos = (posp != NULL) ? *posp : 0;
    PyObject* eight = PyLong_FromLong(8);
    PyObject* bytemask = NULL;
    uint8_t* revbytes = NULL;
    int revbytepos = 0;
    if (out != NULL) {
	bytemask = PyLong_FromLongLong(0x0ff);
	revbytes = PyMem_Malloc(23);
    }
    while (PyObject_IsTrue(val) && (revbytepos < 23)) {
	if (revbytes != NULL) {
	    PyObject* tbyte = PyNumber_And(val, bytemask);
	    revbytes[revbytepos] = PyLong_AsLong(tbyte);
	    Py_DECREF(tbyte);
	}
	revbytepos++;
	val = PyNumber_InPlaceRshift(val, eight);
    }
    if (revbytes != NULL) {
	out[pos] = CBOR_TAG | tag;
	pos++;
	out[pos] = CBOR_BYTES | revbytepos;
	pos++;
	revbytepos--;
	while (revbytepos >= 0) {
	    out[pos] = revbytes[revbytepos];
	    pos++;
	    revbytepos--;
	}
	Py_DECREF(bytemask);
    } else {
	pos += 2 + revbytepos;
    }
    Py_DECREF(eight);
    *posp = pos;
}


// With out=NULL it just counts the length.
// return err, 0=OK
static int inner_dumps(PyObject* ob, uint8_t* out, uintptr_t* posp) {
    uintptr_t pos = (posp != NULL) ? *posp : 0;

    if (PyBool_Check(ob)) {
	if (out != NULL) {
	    if (PyObject_IsTrue(ob)) {
		out[pos] = CBOR_TRUE;
	    } else {
		out[pos] = CBOR_FALSE;
	    }
	}
	pos += 1;
    } else if (ob == Py_None) {
	if (out != NULL) {
	    out[pos] = CBOR_NULL;
	}
	pos += 1;
    } else if (PyDict_Check(ob)) {
	int err = dumps_dict(ob, out, &pos);
	if (err != 0) { return err; }
    } else if (PyList_Check(ob)) {
        Py_ssize_t i;
	Py_ssize_t listlen = PyList_Size(ob);
	tag_aux_out(CBOR_ARRAY, listlen, out, &pos);
	for (i = 0; i < listlen; i++) {
	    int err = inner_dumps(PyList_GetItem(ob, i), out, &pos);
	    if (err != 0) { return err; }
	}
    } else if (PyTuple_Check(ob)) {
        Py_ssize_t i;
	Py_ssize_t listlen = PyTuple_Size(ob);
	tag_aux_out(CBOR_ARRAY, listlen, out, &pos);
	for (i = 0; i < listlen; i++) {
	    int err = inner_dumps(PyTuple_GetItem(ob, i), out, &pos);
	    if (err != 0) { return err; }
	}
	// TODO: accept other enumerables and emit a variable length array
#ifdef Py_INTOBJECT_H
	// PyInt exists in Python 2 but not 3
    } else if (PyInt_Check(ob)) {
	long val = PyInt_AsLong(ob);
	if (val >= 0) {
	    tag_aux_out(CBOR_UINT, val, out, &pos);
	} else {
	    tag_aux_out(CBOR_NEGINT, -1 - val, out, &pos);
	}
#endif
    } else if (PyLong_Check(ob)) {
	int overflow = 0;
	long long val = PyLong_AsLongLongAndOverflow(ob, &overflow);
	if (overflow == 0) {
	    if (val >= 0) {
		tag_aux_out(CBOR_UINT, val, out, &pos);
	    } else {
		tag_aux_out(CBOR_NEGINT, -1L - val, out, &pos);
	    }
	} else {
	    if (overflow < 0) {
		// BIG NEGINT
		PyObject* minusone = PyLong_FromLongLong(-1L);
		PyObject* val = PyNumber_Subtract(minusone, ob);
		Py_DECREF(minusone);
		dumps_bignum(CBOR_TAG_NEGBIGNUM, val, out, &pos);
		Py_DECREF(val);
	    } else {
		// BIG INT
		dumps_bignum(CBOR_TAG_BIGNUM, ob, out, &pos);
	    }
	}
    } else if (PyFloat_Check(ob)) {
	double val = PyFloat_AsDouble(ob);
	tag_u64_out(CBOR_7, *((uint64_t*)(&val)), out, &pos);
    } else if (PyBytes_Check(ob)) {
	Py_ssize_t len = PyBytes_Size(ob);
	tag_aux_out(CBOR_BYTES, len, out, &pos);
	if (out != NULL) {
	    memcpy(out + pos, PyBytes_AsString(ob), len);
	}
	pos += len;
    } else if (PyUnicode_Check(ob)) {
	PyObject* utf8 = PyUnicode_AsUTF8String(ob);
	Py_ssize_t len = PyBytes_Size(utf8);
	tag_aux_out(CBOR_TEXT, len, out, &pos);
	if (out != NULL) {
	    memcpy(out + pos, PyBytes_AsString(utf8), len);
	}
	pos += len;
	Py_DECREF(utf8);
    } else {
	PyErr_Format(PyExc_ValueError, "cannot serialize unknown object: %R", ob);
	return -1;
    }
    if (posp != NULL) {
	*posp = pos;
    }
    return 0;
}

static PyObject*
cbor_dumps(PyObject* noself, PyObject* args) {
    PyObject* ob;
    is_big_endian();
    if (PyType_IsSubtype(Py_TYPE(args), &PyList_Type)) {
	ob = PyList_GetItem(args, 0);
    } else if (PyType_IsSubtype(Py_TYPE(args), &PyTuple_Type)) {
	ob = PyTuple_GetItem(args, 0);
    } else {
	PyErr_Format(PyExc_ValueError, "args not list or tuple: %R\n", args);
	return NULL;
    }

    {
	Py_ssize_t outlen = 0;
	uintptr_t pos = 0;
	void* out = NULL;
	PyObject* obout = NULL;
	int err;

	// first pass just to count length
	err = inner_dumps(ob, NULL, &pos);
	if (err != 0) {
	    return NULL;
	}

	outlen = pos;

	out = PyMem_Malloc(outlen);
	if (out == NULL) {
	    PyErr_NoMemory();
	    return NULL;
	}

	err = inner_dumps(ob, out, NULL);
	if (err != 0) {
	    PyMem_Free(out);
	    return NULL;
	}

	// TODO: I wish there was a way to do this without this copy.
	obout = PyBytes_FromStringAndSize(out, outlen);
	PyMem_Free(out);
	return obout;
    }
}

static PyObject*
cbor_dump(PyObject* noself, PyObject* args) {
    // args should be (obj, fp)
    PyObject* ob;
    PyObject* fp;

    is_big_endian();
    if (PyType_IsSubtype(Py_TYPE(args), &PyList_Type)) {
	ob = PyList_GetItem(args, 0);
	fp = PyList_GetItem(args, 1);
    } else if (PyType_IsSubtype(Py_TYPE(args), &PyTuple_Type)) {
	ob = PyTuple_GetItem(args, 0);
	fp = PyTuple_GetItem(args, 1);
    } else {
	PyErr_Format(PyExc_ValueError, "args not list or tuple: %R\n", args);
	return NULL;
    }

    {
	// TODO: make this smarter, right now it is justt fp.write(dumps(ob))
	Py_ssize_t outlen = 0;
	uintptr_t pos = 0;
	void* out = NULL;
	PyObject* obout = NULL;
	int err;

	// first pass just to count length
	err = inner_dumps(ob, NULL, &pos);
	if (err != 0) {
	    return NULL;
	}

	outlen = pos;

	out = PyMem_Malloc(outlen);
	if (out == NULL) {
	    PyErr_NoMemory();
	    return NULL;
	}

	err = inner_dumps(ob, out, NULL);
	if (err != 0) {
	    PyMem_Free(out);
	    return NULL;
	}

#if HAS_FILE_READER
	if (PyFile_Check(fp)) {
	    FILE* fout = PyFile_AsFile(fp);
	    fwrite(out, 1, outlen, fout);
	} else
#endif
	{
	    PyObject* ret;
#if IS_PY3
	    PyObject* writeStr = PyUnicode_FromString("write");
#else
	    PyObject* writeStr = PyString_FromString("write");
#endif
	    obout = PyBytes_FromStringAndSize(out, outlen);
	    //fprintf(stderr, "write %zd bytes to %p.write() as %p\n", outlen, fp, obout);
	    ret = PyObject_CallMethodObjArgs(fp, writeStr, obout, NULL);
	    Py_DECREF(writeStr);
	    Py_DECREF(ret);
	    Py_DECREF(obout);
	    //fprintf(stderr, "wrote %zd bytes to %p.write() as %p\n", outlen, fp, obout);
	}
	PyMem_Free(out);
    }

    return Py_None;
}


static PyMethodDef CborMethods[] = {
    {"loads",  cbor_loads, METH_VARARGS,
        "parse cbor from data buffer to objects"},
    {"dumps", cbor_dumps, METH_VARARGS,
        "serialize python object to bytes"},
    {"load",  cbor_load, METH_VARARGS,
     "Parse cbor from data buffer to objects.\n"
     "Takes a file-like object capable of .read(N)\n"},
    {"dump", cbor_dump, METH_VARARGS,
     "Serialize python object to bytes.\n"
     "dump(obj, fp)\n"
     "obj: object to output; fp: file-like object to .write() to\n"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

#ifdef Py_InitModule
// Python 2.7
PyMODINIT_FUNC
init_cbor(void)
{
    (void) Py_InitModule("cbor._cbor", CborMethods);
}
#else
// Python 3
PyMODINIT_FUNC
PyInit__cbor(void)
{
    static PyModuleDef modef = {
	PyModuleDef_HEAD_INIT,
    };
    //modef.m_base = PyModuleDef_HEAD_INIT;
    modef.m_name = "cbor._cbor";
    modef.m_doc = NULL;
    modef.m_size = 0;
    modef.m_methods = CborMethods;
    modef.m_reload = NULL;
    modef.m_traverse = NULL;
    modef.m_clear = NULL;
    modef.m_free = NULL;
    return PyModule_Create(&modef);
}
#endif

