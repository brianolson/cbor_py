#include "Python.h"

#include "cbor.h"

#include <math.h>
#include <stdint.h>


static PyObject* inner_loads(uint8_t* raw, uintptr_t* posp) {
    uintptr_t pos = *posp;
    uint8_t c = raw[pos];
    uint8_t cbor_type = c & CBOR_TYPE_MASK;
    uint8_t cbor_info = c & CBOR_INFO_BITS;
    uint64_t aux;
    if (cbor_info <= 23) {
	// literal value <=23
	aux = cbor_info;
	pos += 1;
    } else if (cbor_info == CBOR_UINT8_FOLLOWS) {
	aux = raw[pos + 1];
	pos += 1;
    } else if (cbor_info == CBOR_UINT16_FOLLOWS) {
	if (cbor_type == CBOR_7) { // float16
	    // float16 parsing adapted from example code in spec
	    uint8_t hibyte = raw[pos+1];
	    int exp = (hibyte >> 2) & 0x1f;
	    int mant = ((hibyte & 0x3) << 8) | raw[pos+2];
	    double val;
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
	    *posp = pos + 3;
	    return PyFloat_FromDouble(val);
	}
	aux = (raw[pos + 1] << 8) | raw[pos + 2];
	pos += 3;
    } else if (cbor_info == CBOR_UINT32_FOLLOWS) {
	if (cbor_type == CBOR_7) { // float32
	    float val;
#if BIG_ENDIAN
	    // easy!
	    void* dest = (void*)(&val);
	    memcpy(dest, raw + pos + 1, 4);
#elif LITTLE_ENDIAN
	    uint8_t* dest = (uint8_t*)(&val);
	    dest[3] = raw[pos + 1];
	    dest[2] = raw[pos + 2];
	    dest[1] = raw[pos + 3];
	    dest[0] = raw[pos + 4];
#else
#error "endianness undefined"
#endif
	    *posp = pos + 5;
	    return PyFloat_FromDouble(val);
	}
	aux = 
	    (raw[pos + 1] << 24) |
	    (raw[pos + 2] << 16) |
	    (raw[pos + 3] <<  8) |
	    raw[pos + 4];
	pos += 5;
    } else if (cbor_info == CBOR_UINT64_FOLLOWS) {
	aux = 0;
	for (int si = 1; si <= 8; si++) {
	    aux = aux << 8;
	    aux |= raw[pos + si];
	}
	pos += 9;
    } else {
	// raise Exception("bogus tag info number")
	aux = 0;
    }
    PyObject* out = NULL;
    switch (cbor_type) {
    case CBOR_UINT:
	out = PyLong_FromLong((long)aux);
	break;
    case CBOR_NEGINT:
	out = PyLong_FromLong((long)(-1 - aux));
	break;
    case CBOR_BYTES:
	if (cbor_info == CBOR_VAR_FOLLOWS) {
	    // TODO: WRITEME
	    assert(0);
	} else {
	    out = PyBytes_FromStringAndSize((char*)(raw + pos), (Py_ssize_t)aux);
	    pos += aux;
	}
	break;
    case CBOR_TEXT:
	if (cbor_info == CBOR_VAR_FOLLOWS) {
	    // TODO: WRITEME
	    assert(0);
	} else {
	    out = PyUnicode_FromStringAndSize((char*)(raw + pos), (Py_ssize_t)aux);
	    pos += aux;
	}
	break;
    case CBOR_ARRAY:
	// TODO: WRITEME
	assert(0);
	if (cbor_info == CBOR_VAR_FOLLOWS) {

	} else {
	}
	break;
    case CBOR_MAP:
	// TODO: WRITEME
	assert(0);
	if (cbor_info == CBOR_VAR_FOLLOWS) {

	} else {
	}
	break;
    case CBOR_TAG:
	// TODO: WRITEME
	// return an object CBORTag(tagnum, nextob)
	assert(0);
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
    *posp = pos;
    return out;
}

static PyObject*
cbor_loads(PyObject* noself, PyObject* args) {
    PyObject* ob;
    if (PyType_IsSubtype(Py_TYPE(args), &PyList_Type)) {
	ob = PyList_GetItem(args, 0);
    } else if (PyType_IsSubtype(Py_TYPE(args), &PyTuple_Type)) {
	ob = PyTuple_GetItem(args, 0);
    } else {
	PyObject* repr = PyObject_Repr(args);
	fprintf(stderr, "args not list or tuple: %s\n",
		PyString_AsString(repr));
	Py_DECREF(repr);
	return NULL;
    }

    {
	uint8_t* raw = (uint8_t*)PyBytes_AsString(ob);
	uintptr_t pos = 0;
	return inner_loads(raw, &pos);
    }

    // ob should be bytes or string data to parse
    return NULL;
}

static PyMethodDef CborMethods[] = {
    {"loads",  cbor_loads, METH_VARARGS,
        "parse cbor from data buffer to objects"},
#if 0
    {"dumps", cbor_dumps, METH_VARARGS,
        "serialize python object to bytes"},
#endif
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

PyMODINIT_FUNC
init_cbor(void)
{
    (void) Py_InitModule("cbor._cbor", CborMethods);
}
