#include "Python.h"

#include "cbor.h"

#include <math.h>
#include <stdint.h>

#include <stdio.h>

static PyObject* loads_tag(uint8_t* raw, uintptr_t* posp, Py_ssize_t len, uint64_t aux);


static PyObject* inner_loads(uint8_t* raw, uintptr_t* posp, Py_ssize_t len) {
    uintptr_t pos = *posp;
    uint8_t c = raw[pos];
    uint8_t cbor_type = c & CBOR_TYPE_MASK;
    uint8_t cbor_info = c & CBOR_INFO_BITS;
    uint64_t aux;

    //fprintf(stderr, "inner_loads(%p, %lu, %ld) type=%d info=%d\n", raw, pos, len, cbor_type >> 5, cbor_info);

    if (pos > len) {
	PyErr_SetString(PyExc_ValueError, "misparse, token went longer than buffer");
	fprintf(stderr, "ERROR raise Exception(\"misparse, token went longer than buffer\")\n");
	return NULL;
    }

    pos += 1;
    if (cbor_info <= 23) {
	// literal value <=23
	aux = cbor_info;
    } else if (cbor_info == CBOR_UINT8_FOLLOWS) {
	aux = raw[pos];
	pos += 1;
    } else if (cbor_info == CBOR_UINT16_FOLLOWS) {
	if (cbor_type == CBOR_7) { // float16
	    // float16 parsing adapted from example code in spec
	    uint8_t hibyte = raw[pos];
	    int exp = (hibyte >> 2) & 0x1f;
	    int mant = ((hibyte & 0x3) << 8) | raw[pos+1];
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
	    *posp = pos + 2;
	    return PyFloat_FromDouble(val);
	}
	aux = (raw[pos] << 8) | raw[pos + 1];
	pos += 2;
    } else if (cbor_info == CBOR_UINT32_FOLLOWS) {
	if (cbor_type == CBOR_7) { // float32
	    float val;
#if BIG_ENDIAN
	    // easy!
	    void* dest = (void*)(&val);
	    memcpy(dest, raw + pos, 4);
#elif LITTLE_ENDIAN
	    uint8_t* dest = (uint8_t*)(&val);
	    dest[3] = raw[pos + 0];
	    dest[2] = raw[pos + 1];
	    dest[1] = raw[pos + 2];
	    dest[0] = raw[pos + 3];
#else
#error "endianness undefined"
#endif
	    *posp = pos + 4;
	    return PyFloat_FromDouble(val);
	}
	aux = 
	    (raw[pos + 0] << 24) |
	    (raw[pos + 1] << 16) |
	    (raw[pos + 2] <<  8) |
	    raw[pos + 3];
	pos += 4;
    } else if (cbor_info == CBOR_UINT64_FOLLOWS) {
	aux = 0;
	for (int si = 0; si < 8; si++) {
	    aux = aux << 8;
	    aux |= raw[pos + si];
	}
	pos += 8;
	if (cbor_type == CBOR_7) {  // float64
	    *posp = pos;
	    return PyFloat_FromDouble(*((double*)(&aux)));
	}
    } else {
	aux = 0;
    }
    PyObject* out = NULL;
    switch (cbor_type) {
    case CBOR_UINT:
	out = PyLong_FromUnsignedLongLong(aux);
	break;
    case CBOR_NEGINT:
	out = PyLong_FromLongLong((long long)(((long long)-1) - aux));
	break;
    case CBOR_BYTES:
	if (cbor_info == CBOR_VAR_FOLLOWS) {
	    fprintf(stderr, "TODO: WRITEME CBOR VAR BYTES\n");
	    assert(0);
	} else {
	    out = PyBytes_FromStringAndSize((char*)(raw + pos), (Py_ssize_t)aux);
	    pos += aux;
	}
	break;
    case CBOR_TEXT:
	if (cbor_info == CBOR_VAR_FOLLOWS) {
	    fprintf(stderr, "TODO: WRITEME CBOR VAR TEXT\n");
	    assert(0);
	} else {
	    out = PyUnicode_FromStringAndSize((char*)(raw + pos), (Py_ssize_t)aux);
	    pos += aux;
	}
	break;
    case CBOR_ARRAY:
	if (cbor_info == CBOR_VAR_FOLLOWS) {
	    fprintf(stderr, "TODO: WRITEME CBOR VAR ARRAY\n");
	} else {
	    out = PyList_New((Py_ssize_t)aux);
	    for (unsigned int i = 0; i < aux; i++) {
		PyObject* subitem = inner_loads(raw, &pos, len);
		if (subitem == NULL) {
		    fprintf(stderr, "error building list at item %d of %llu\n", i, aux);
		    return NULL;
		}
		PyList_SetItem(out, (Py_ssize_t)i, subitem);
	    }
	}
	break;
    case CBOR_MAP:
	out = PyDict_New();
	if (cbor_info == CBOR_VAR_FOLLOWS) {
	    fprintf(stderr, "TODO: WRITEME CBOR VAR MAP\n");
	} else {
	    //fprintf(stderr, "loading dict of %llu\n", aux);
	    for (unsigned int i = 0; i < aux; i++) {
		PyObject* key = inner_loads(raw, &pos, len);
		PyObject* value;
		if (key == NULL) {
		    fprintf(stderr, "error building map at key %d of %llu\n", i, aux);
		    return NULL;
		}
		value = inner_loads(raw, &pos, len);
		if (key == NULL) {
		    fprintf(stderr, "error building map at value %d of %llu\n", i, aux);
		    return NULL;
		}
		PyDict_SetItem(out, key, value);
		//fprintf(stderr, "loaded dict [%u] of %llu\n", i, aux);
	    }
	}
	break;
    case CBOR_TAG:
	out = loads_tag(raw, &pos, len, aux);
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


static PyObject* loads_bignum(uint8_t* raw, uintptr_t* posp, Py_ssize_t len) {
    uintptr_t pos = *posp;
    PyObject* out = NULL;

    uint8_t bytes_info = raw[pos] & CBOR_INFO_BITS;
    if (bytes_info < 24) {
	PyObject* eight = PyInt_FromLong(8);
	out = PyLong_FromLong(0);
	pos++;
	for (int i = 0; i < bytes_info; i++) {
	    // TODO: is this leaking like crazy?
	    PyObject* curbyte;
	    PyObject* tout = PyNumber_Lshift(out, eight);
	    Py_DECREF(out);
	    out = tout;
	    curbyte = PyInt_FromLong(raw[pos]);
	    tout = PyNumber_Or(out, curbyte);
	    Py_DECREF(curbyte);
	    Py_DECREF(out);
	    out = tout;
	    pos++;
	}
	*posp = pos;
	return out;
    } else {
	fprintf(stderr, "TODO: TAG BIGNUM bytes_info=%d, len(ull)=%lu\n", bytes_info, sizeof(unsigned long long));
    }
    return NULL;
}


static PyObject* loads_tag(uint8_t* raw, uintptr_t* posp, Py_ssize_t len, uint64_t aux) {
    uintptr_t pos = *posp;
    PyObject* out = NULL;
    // return an object CBORTag(tagnum, nextob)
    if (aux == CBOR_TAG_BIGNUM) {
	// If the next object is bytes, interpret it here without making a PyObject for it.
	if ((raw[pos] & CBOR_TYPE_MASK) == CBOR_BYTES) {
	    out = loads_bignum(raw, &pos, len);
	    if (out != NULL) {
		*posp = pos;
		return out;
	    }
	} else {
	    fprintf(stderr, "raise Exception('TAG BIGNUM not followed by bytes but %02x')\n", raw[pos]);
	}
	fprintf(stderr, "TODO: WRITEME CBOR TAG BIGNUM %02x %02x\n", raw[pos], raw[pos+1]);
    } else if (aux == CBOR_TAG_NEGBIGNUM) {
	// If the next object is bytes, interpret it here without making a PyObject for it.
	if ((raw[pos] & CBOR_TYPE_MASK) == CBOR_BYTES) {
	    out = loads_bignum(raw, &pos, len);
	    if (out != NULL) {
		PyObject* minusOne = PyLong_FromLong(-1);
		PyObject* tout = PyNumber_Subtract(minusOne, out);
		Py_DECREF(minusOne);
		Py_DECREF(out);
		out = tout;
		*posp = pos;
		return out;
	    }
	} else {
	    fprintf(stderr, "raise Exception('TAG NEGBIGNUM not followed by bytes but %02x')\n", raw[pos]);
	}
	fprintf(stderr, "TODO: WRITEME CBOR TAG NEGBIGNUM %02x %02x\n", raw[pos], raw[pos+1]);
    } else {
	fprintf(stderr, "TODO: WRITEME CBOR TAG %llu\n", aux);
    }
    out = inner_loads(raw, &pos, len);
    if (out == NULL) {
	fprintf(stderr, "error loading tagged object %02x %02x\n", raw[pos], raw[pos+1]);
	return NULL;
    }
    {
	PyObject* cbor_module = PyImport_ImportModule("cbor");
	PyObject* moddict = PyModule_GetDict(cbor_module);
	PyObject* tag_class = PyDict_GetItemString(moddict, "Tag");
	PyObject* args = Py_BuildValue("(K,o)", aux, out);
	PyObject* tout = PyInstance_New(tag_class, args, Py_None);
	Py_DECREF(out);
	Py_DECREF(args);
	Py_DECREF(tag_class);
	// moddict is a 'borrowed reference'
	Py_DECREF(cbor_module);
	out = tout;
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
	Py_ssize_t len = PyBytes_Size(ob);
	uintptr_t pos = 0;
	return inner_loads(raw, &pos, len);
    }
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

static void inner_dumps(PyObject* ob, uint8_t* out, uintptr_t* posp);

static void dumps_dict(PyObject* ob, uint8_t* out, uintptr_t* posp) {
    uintptr_t pos = *posp;
    Py_ssize_t dictiter = 0;
    PyObject* key;
    PyObject* val;
    Py_ssize_t dictlen = PyDict_Size(ob);
    tag_aux_out(CBOR_MAP, dictlen, out, &pos);
    while (PyDict_Next(ob, &dictiter, &key, &val)) {
	inner_dumps(key, out, &pos);
	inner_dumps(val, out, &pos);
    }
    *posp = pos;
}


static void dumps_bignum(uint8_t tag, PyObject* val, uint8_t* out, uintptr_t* posp) {
    uintptr_t pos = (posp != NULL) ? *posp : 0;
    PyObject* eight = PyInt_FromLong(8);
    PyObject* bytemask = NULL;
    uint8_t* revbytes = NULL;
    int revbytepos = 0;
    if (out != NULL) {
	//fprintf(stderr, "dumps_bignum with output\n");
	bytemask = PyLong_FromLongLong(0x0ff);
	revbytes = PyMem_Malloc(23);
    } else {
	//fprintf(stderr, "dumps_bignum just count\n");
    }
    while (PyObject_IsTrue(val) && (revbytepos < 23)) {
	if (revbytes != NULL) {
	    PyObject* tbyte = PyNumber_And(val, bytemask);
	    revbytes[revbytepos] = PyLong_AsLong(tbyte);
	    Py_DECREF(tbyte);
	}
	revbytepos++;
	val = PyNumber_InPlaceRshift(val, eight);
	//fprintf(stderr, "dumps_bignum count pos=%d\n", revbytepos);
    }
    if (revbytes != NULL) {
	out[pos] = CBOR_TAG | tag;
	pos++;
	out[pos] = CBOR_BYTES | revbytepos;
	pos++;
	revbytepos--;
	while (revbytepos >= 0) {
	    //fprintf(stderr, "dumps_bignum  out pos=%d\n", revbytepos);
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
    //fprintf(stderr, "dumps_bignum END\n");
}


// With out=NULL it just counts the length.
static void inner_dumps(PyObject* ob, uint8_t* out, uintptr_t* posp) {
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
	dumps_dict(ob, out, &pos);
    } else if (PyList_Check(ob)) {
	Py_ssize_t listlen = PyList_Size(ob);
	tag_aux_out(CBOR_ARRAY, listlen, out, &pos);
	for (Py_ssize_t i = 0; i < listlen; i++) {
	    inner_dumps(PyList_GetItem(ob, i), out, &pos);
	}
    } else if (PyInt_Check(ob)) {
	long val = PyInt_AsLong(ob);
	if (val >= 0) {
	    tag_aux_out(CBOR_UINT, val, out, &pos);
	} else {
	    tag_aux_out(CBOR_NEGINT, -1 - val, out, &pos);
	}
    } else if (PyLong_Check(ob)) {
	int overflow = 0;
	long long val = PyLong_AsLongLongAndOverflow(ob, &overflow);
	if (overflow == 0) {
	    if (val >= 0) {
		tag_aux_out(CBOR_UINT, val, out, &pos);
	    } else {
		tag_aux_out(CBOR_NEGINT, val, out, &pos);
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
	fprintf(stderr, "TODO: unknown object!\n");
    }
    if (posp != NULL) {
	*posp = pos;
    }
}

static PyObject*
cbor_dumps(PyObject* noself, PyObject* args) {
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
	Py_ssize_t outlen = 0;
	uintptr_t pos = 0;
	void* out = NULL;
	PyObject* obout = NULL;

	// first pass just to count length
	inner_dumps(ob, NULL, &pos);

	outlen = pos;

	out = PyMem_Malloc(outlen);
	if (out == NULL) {
	    fprintf(stderr, "TODO: raise OutOfMemoryError\n");
	    return NULL;
	}

	inner_dumps(ob, out, NULL);

	obout = PyBytes_FromStringAndSize(out, outlen);
	PyMem_Free(out);
	return obout;
    }
}


static PyMethodDef CborMethods[] = {
    {"loads",  cbor_loads, METH_VARARGS,
        "parse cbor from data buffer to objects"},
    {"dumps", cbor_dumps, METH_VARARGS,
        "serialize python object to bytes"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

PyMODINIT_FUNC
init_cbor(void)
{
    (void) Py_InitModule("cbor._cbor", CborMethods);
}



