/*
 * Copyright (C) 2019 MindMaze
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <Python.h>
#include <structmember.h>
#include <unistd.h>

#include "xdfio.h"

/*
 * References:
 * - https://docs.python.org/3/c-api/
 * - https://docs.scipy.org/doc/numpy/reference/c-api.array.html
 */

/* Define _fseeki64() and _ftelli64() to dummy non-existant symbols
 * in order to silence Wattributes warnings within numpy.
 * Undefine them afterwards just in case */
#define _fseeki64 dummy_fseek_symbol
#define _ftelli64 dummy_ftell_symbol
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>
#undef _fseeki64
#undef _ftelli64

#define XDF_CHANNEL_ENABLE 0
#define XDF_CHANNEL_DISABLE -1

/* custom xdf error */
static PyObject * XdfError = NULL;

/* object holder for struct xdf */
struct pyxdf {
	PyObject_HEAD

	/* internal members */
	struct xdf * xdf;
	int mode; /* shortcut read/write */
	int type; /* used to ensure file flush when writing */

	/* read-only members */
	long len;  /* number of samples */
	char * filename;
	char * filetype;

	/* read-write members */
	PyObject * channels; /* list of dicts */
	double fs; /* sampling rate */

	double record_time;
	PyObject * subject_desc;
	PyObject * session_desc;
};

/*
 * Documentation and function definitions
 */
static PyObject*
_xdf_new(PyTypeObject *type, PyObject *args, PyObject *kwargs);

static int
_xdf_init(struct pyxdf *self, PyObject *args, PyObject *kwargs);

static void
_xdf_destroy(struct pyxdf *self);

static PyObject *
_xdf_read(struct pyxdf *self, PyObject *args, PyObject *kwargs);

static PyObject *
_xdf_write(struct pyxdf *self, PyObject *args);

static struct PyMethodDef pyxdf_methods[] = {
	{"_read", (PyCFunction)_xdf_read, METH_VARARGS|METH_KEYWORDS, "internal"},
	{"_write", (PyCFunction)_xdf_write, METH_VARARGS, "internal"},
	{NULL, 0, 0, NULL}
};

PyMODINIT_FUNC PyInit__pyxdf(void);

/* Python3 module initialization */
static struct PyModuleDef xdf_module =
{
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = "_pyxdf",
	.m_doc = "Python _xdf module documentation\n",
	.m_size = -1, /* the module does not support sub-interpreters */
};

static PyMemberDef pyxdf_members[] = {
	/* read-only members */
	{"len", T_LONG, offsetof(struct pyxdf, len), READONLY,
	 "Number of samples"},
	{"filename", T_STRING, offsetof(struct pyxdf, filename), READONLY,
	 "Name of the opened xdf file"},
	{"filetype", T_STRING, offsetof(struct pyxdf, filetype), READONLY,
	 "Type of the opened xdf file (edf, gdf ...)"},

	/* read-write members */
	{"channels", T_OBJECT_EX, offsetof(struct pyxdf, channels), 0,
	 "Ordered list of the channel descriptions."},
	{"fs", T_DOUBLE, offsetof(struct pyxdf, fs), 0, "Sampling rate"},

	{"record_time", T_DOUBLE, offsetof(struct pyxdf, record_time), 0,
	 "Date and time of recording as a float."},
	{"subject_desc", T_OBJECT_EX, offsetof(struct pyxdf, subject_desc), 0,
	 "String describing the subject"},
	{"session_desc", T_OBJECT_EX, offsetof(struct pyxdf, session_desc), 0,
	 "String describing the session of recording"},

	{NULL, 0, 0, 0, NULL},
};

static PyTypeObject XdfType = {
	PyVarObject_HEAD_INIT(NULL, 0)

	.tp_name = "_XFile",
	.tp_doc = "_xdf python internal object wrapper",
	.tp_basicsize = sizeof(struct pyxdf),
	.tp_itemsize = 0,
	.tp_flags = Py_TPFLAGS_BASETYPE|Py_TPFLAGS_DEFAULT,

	.tp_new = _xdf_new,
	.tp_init = (initproc) _xdf_init,
	.tp_dealloc = (destructor) _xdf_destroy,
	.tp_methods = pyxdf_methods,
	.tp_members = pyxdf_members,
};

PyMODINIT_FUNC PyInit__pyxdf(void)
{
	PyObject * m = PyModule_Create(&xdf_module);
	if (m == NULL)
		return NULL;

	if (PyType_Ready(&XdfType) < 0)
		return NULL;

	/* Allow us to create _XFile classes by calling the "_XFile" class */
	Py_INCREF(&XdfType);
	PyModule_AddObject(m, "_XFile", (PyObject*) &XdfType);

	/* Allow us to create custom xdf exceptions */
	XdfError = PyErr_NewException("xdf.error", NULL, NULL);
	Py_INCREF(XdfError);
	PyModule_AddObject(m, "error", XdfError);

	/* load numpy array API */
	if (_import_array() < 0)
		return NULL;

	return m;
}

/*
 * implementation
 */

/* helpers */
static
int get_xdf_mode(char const * mode_str)
{
	if (mode_str == NULL)
		goto error;
	else if (strcmp(mode_str, "write") == 0 || strcmp(mode_str, "w") == 0)
		return XDF_WRITE|XDF_TRUNC;
	else if (strcmp(mode_str, "wx") == 0 || strcmp(mode_str, "xw") == 0)
		return XDF_WRITE;
	else if (strcmp(mode_str, "read") == 0 || strcmp(mode_str, "r") == 0)
		return XDF_READ;

error:
	PyErr_Format(PyExc_ValueError, "xdf_open(): invalid mode: %s",
	             mode_str);
	return -1;
}


static
int get_xdf_filetype_int(char const * type_str)
{
	if (type_str == NULL)
		goto error;
	else if (strcmp(type_str, "any") == 0)
		return XDF_ANY;
	else if (strcmp(type_str, "edf") == 0)
		return XDF_EDF;
	else if (strcmp(type_str, "edfp") == 0)
		return XDF_EDFP;
	else if (strcmp(type_str, "bdf") == 0)
		return XDF_BDF;
	else if (strcmp(type_str, "gdf1") == 0)
		return XDF_GDF1;
	else if (strcmp(type_str, "gdf2") == 0 || strcmp(type_str, "gdf") == 0)
		return XDF_GDF2;

error:
	PyErr_Format(PyExc_ValueError, "xdf_open(): invalid type: %s",
	             type_str);
	return -1;
}

static
int get_xdf_filetype_str(int type, char * type_str)
{
	switch (type) {
	case XDF_EDF:
		strcpy(type_str, "edf");
		return 0;
	case XDF_EDFP:
		strcpy(type_str, "edfp");
		return 0;
	case XDF_BDF:
		strcpy(type_str, "bdf");
		return 0;
	case XDF_GDF1:
		strcpy(type_str, "gdf1");
		return 0;
	case XDF_GDF2:
		strcpy(type_str, "gdf");
		return 0;

	case XDF_ANY: /* should not be asked */
	default:
		*type_str = '\0';
		return -1;
	}
}

/* API */
static PyObject*
_xdf_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
	(void) args;
	(void) kwargs;

	struct pyxdf *self = (struct pyxdf*) type->tp_alloc(type, 0);
	return (PyObject*) self;
}


static
int read_xdf_metadata(struct pyxdf *self)
{
	int i, rec_ns, nrec;
	int tmp_fs; /* xdffileio store the frequency as in integer */
	struct xdfch * ch;
	PyObject* pych;
	double phy_min, phy_max;
	int tmp_type;
	char tmp_type_str[8];
	char * tmp_subject_str, * tmp_sess_str, * channel_name, * ch_unit_label;

	if (xdf_get_conf(self->xdf,
	                 XDF_F_FILEFMT, &tmp_type,
	                 XDF_F_REC_NSAMPLE, &rec_ns,
	                 XDF_F_NREC, &nrec,
	                 XDF_F_SAMPLING_FREQ, &tmp_fs,
	                 XDF_F_RECTIME, &self->record_time,
	                 XDF_F_SUBJ_DESC, &tmp_subject_str,
	                 XDF_F_SESS_DESC, &tmp_sess_str,
	                 XDF_NOF) != 0)
		return -1;

	self->len = rec_ns * nrec;
	self->fs = (double) tmp_fs; /* C: int -> py: float */
	self->subject_desc = PyUnicode_FromString(tmp_subject_str);
	self->session_desc = PyUnicode_FromString(tmp_sess_str);

	if (get_xdf_filetype_str(tmp_type, tmp_type_str) != 0)
		return -1;

	self->filetype = strdup(tmp_type_str);

	if (self->filename == NULL || self->filetype == NULL)
		return -1;

	i = 0;
	while ((ch = xdf_get_channel(self->xdf, i)) != NULL) {
		if (xdf_get_chconf(ch, XDF_CF_LABEL, &channel_name,
		                   XDF_CF_PMIN, &phy_min,
		                   XDF_CF_PMAX, &phy_max,
		                   XDF_CF_UNIT, &ch_unit_label,
		                   XDF_NOF) != 0)
			return -1;

		pych = PyDict_New();
		if (pych == NULL)
			return -1;

		if (PyDict_SetItemString(pych, "name", Py_BuildValue("s", channel_name)) != 0
		    || PyDict_SetItemString(pych, "physical_min", Py_BuildValue("d", phy_min)) != 0
		    || PyDict_SetItemString(pych, "physical_max", Py_BuildValue("d", phy_max)) != 0
		    || PyDict_SetItemString(pych, "unit", Py_BuildValue("s", ch_unit_label)) != 0)
			goto error;

		if (PyList_Append(self->channels, pych) == -1)
			goto error;

		Py_DECREF(pych);

		i++;
	}

	return 0;

error:

	Py_DECREF(pych);
	return -1;
}


static
int _xdf_init(struct pyxdf *self, PyObject *args, PyObject *kwargs)
{
	char * filename;
	char * mode_str;
	char * type_str = "any";
	int mode, type;

	char * kwlist[] = {"filename", "mode", "type", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|s:open",
	                                 kwlist, &filename, &mode_str,
	                                 &type_str)) {
		return -1;
	}

	mode = get_xdf_mode(mode_str);
	type = get_xdf_filetype_int(type_str);
	if (mode < 0 || type < 0)
		goto error;

	self->xdf = xdf_open(filename, mode, type);
	if (self->xdf == NULL)
		goto error;

	self->mode = mode;
	self->type = type;
	self->filename = strdup(filename);
	self->channels = PyList_New(0);
	self->subject_desc = Py_None;
	self->session_desc = Py_None;

	if ((mode & XDF_READ) == 0) {
		if (type == XDF_GDF1) { /* FIXME: gdf1 does not work for writing */
			PyErr_SetString(PyExc_NotImplementedError,
			                "gdf1 format is not supported");
			return -1;
		}

		self->filetype = strdup(type_str);
		return 0;
	}

	if (read_xdf_metadata(self) != 0)
		goto error;

	return 0;

error:
	/* python will call _xdf_destroy() if _xdf_init() fails */

	/* xdffileio handles errors using errno.
	 * Convert it to an exception and return */
	PyErr_SetFromErrno(PyExc_IOError);
	return -1;
}


static void
_xdf_destroy(struct pyxdf *self)
{
	if (self->xdf)
		xdf_close(self->xdf);

	if (self->channels)
		Py_DECREF(self->channels);

	if (self->session_desc)
		Py_DECREF(self->session_desc);

	if (self->subject_desc)
		Py_DECREF(self->subject_desc);

	free(self->filename);
	free(self->filetype);

	Py_TYPE(self)->tp_free((PyObject*) self);
}


static
struct xdfch * get_xdf_channel(struct xdf * xdf, char const * name)
{
	int i;
	struct xdfch * ch;
	char * label;

	i = 0;
	while ((ch = xdf_get_channel(xdf, i)) != NULL) {

		if (xdf_get_chconf(ch, XDF_CF_LABEL, &label, XDF_NOF) == 0
		    && strcmp(label, name) == 0)
			return ch;
		i++;
	}

	return NULL;
}

static
int filter_channels(struct pyxdf *self, PyObject * channels)
{
	int i, len, enable_channels;
	struct xdfch * ch;
	PyObject * pych;

	i = 0;
	enable_channels = (channels == Py_None) ? XDF_CHANNEL_ENABLE : XDF_CHANNEL_DISABLE;
	while ((ch = xdf_get_channel(self->xdf, i)) != NULL) {
		xdf_set_chconf(ch, XDF_CF_ARRINDEX, enable_channels,
		               XDF_CF_ARROFFSET, i * sizeof(double),
		               XDF_CF_ARRTYPE, XDFDOUBLE,
		               XDF_CF_ARRDIGITAL, 0,
		               XDF_NOF);
		i++;
	}
	if (channels == Py_None)
		return i; /* all channels enabled */

	i = 0;
	len = PyList_Size(channels); /* prevent raising IndexError */
	while (i < len && (pych = PyList_GetItem(channels, i)) != NULL) {
		ch = get_xdf_channel(self->xdf, PyUnicode_AsUTF8(pych));
		if (ch == NULL)
			return -1;

		xdf_set_chconf(ch, XDF_CF_ARRINDEX, XDF_CHANNEL_ENABLE,
		               XDF_CF_ARROFFSET, i * sizeof(double),
		               XDF_NOF);

		i++;
	}

	return i;
}


static PyObject*
_xdf_read(struct pyxdf *self, PyObject *args, PyObject *kwargs)
{
	int rv, nb_nch, ns;
	size_t stride[1];
	double * buffer;
	PyArrayObject * vecout;
	long start, end;
	PyObject * channels = Py_None;

	char * kwlist[] = {"channels", "chunk", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O(ll):read", kwlist,
	                                 &channels, &start, &end))
		return NULL;

	if (self->xdf == NULL) {
		PyErr_BadArgument();
		return NULL;
	}

	if (self->mode != XDF_READ) {
		PyErr_SetString(XdfError,
		                "Can only call read method on xdf object explicitly "
		                "opened for reading");
		return NULL;
	}

	nb_nch = filter_channels(self, channels);
	if (nb_nch <= 0)
		goto error;

	stride[0] = nb_nch * sizeof(double);
	xdf_define_arrays(self->xdf, 1, stride);
	if (xdf_prepare_transfer(self->xdf))
		goto error;

	ns = end - start + 1;

	/* build & return a ns * nch numpy array */
	npy_intp dimensions[2] = {ns, nb_nch};
	vecout = (PyArrayObject*) PyArray_SimpleNew(2, dimensions, NPY_DOUBLE);
	if (vecout == NULL)
		goto error;

	buffer = PyArray_DATA(vecout);

	/* case where only a chunk should be read */
	if (start != 0)
		if (xdf_seek(self->xdf, start, SEEK_SET) == -1)
			goto error;

	rv = xdf_read(self->xdf, ns, buffer);
	if (rv == -1)
		goto error;

	if (xdf_end_transfer(self->xdf) != 0)
		goto error;

	return PyArray_Return(vecout);

error:
	/* xdffileio handles errors using errno.
	 * Convert it to an exception and return */
	xdf_end_transfer(self->xdf);
	PyErr_SetFromErrno(PyExc_IOError);
	return NULL;
}


static
int _xdf_prepare_writing(struct pyxdf *self, int nch)
{
	int i;
	size_t stride[1];
	PyObject * chdesc;
	struct xdfch * ch;

	/* ensure we have defined channels coherent with the data in argument */
	if (PyList_Size(self->channels) != nch) {
		PyErr_SetString(XdfError,
		                "Channel description and argument data are inconsistent.");
		return -1;
	}

	xdf_set_conf(self->xdf,
	             XDF_F_SAMPLING_FREQ, (int) self->fs,
	             XDF_F_RECTIME, self->record_time,
	             XDF_F_SUBJ_DESC, self->subject_desc == Py_None ? "" : PyUnicode_AsUTF8(self->subject_desc),
	             XDF_F_SESS_DESC, self->session_desc == Py_None ? "" : PyUnicode_AsUTF8(self->session_desc),
	             XDF_CF_ARRINDEX, 0,
	             XDF_CF_ARROFFSET, 0,
	             XDF_CF_ARRTYPE, XDFDOUBLE,
	             XDF_CF_ARRDIGITAL, 0,
	             XDF_NOF);

	for (i = 0; i < nch; i++) {
		ch = xdf_add_channel(self->xdf, NULL);
		chdesc = PyList_GetItem(self->channels, i);
		Py_INCREF(chdesc);
		xdf_set_chconf(ch,
		               XDF_CF_LABEL, PyUnicode_AsUTF8(PyDict_GetItemString(chdesc, "name")),
		               XDF_CF_PMIN, PyFloat_AsDouble(PyDict_GetItemString(chdesc, "physical_min")),
		               XDF_CF_PMAX, PyFloat_AsDouble(PyDict_GetItemString(chdesc, "physical_max")),
		               XDF_CF_UNIT, PyUnicode_AsUTF8(PyDict_GetItemString(chdesc, "unit")),
		               XDF_NOF);
		Py_DECREF(chdesc);
	}

	stride[0] = nch * sizeof(double);
	xdf_define_arrays(self->xdf, 1, stride);
	if (xdf_prepare_transfer(self->xdf))
		return -1;

	return 0;
}


static
PyObject* _xdf_write(struct pyxdf *self, PyObject *args)
{
	int nch, ns;
	void * buffer;
	PyObject * array = Py_None;
	int ndims;
	npy_intp * dims;

	if (!PyArg_ParseTuple(args, "O:write", &array))
		return NULL;

	if (array == Py_None)
		goto error;

	/* Prepare for data transfer */
	ndims = PyArray_NDIM((PyArrayObject*) array);
	dims = PyArray_DIMS((PyArrayObject*) array);
	buffer = PyArray_DATA((PyArrayObject*) array);

	if (ndims != 2) {
		PyErr_SetString(XdfError,
		                "Malformed or invalid input data. "
		                "xdffileio only supports 2D arrays");
		return NULL;
	}

	/* we expect a buffer of dimensions ns * nch
	 * alias to those names. */
	ns = dims[0];
	nch = dims[1];
	if (_xdf_prepare_writing(self, nch) != 0)
		goto error;

	/* re-open xdf file to re-write in it */
	if (self->xdf == NULL)
		self->xdf = xdf_open(self->filename, self->mode, self->type);

	if (xdf_write(self->xdf, ns, buffer) != ns)
		goto error;

	/* close xdf file to force flushing */
	xdf_close(self->xdf);
	self->xdf = NULL;

	return Py_None;

error:
	/* xdffileio handles errors using errno.
	 * Convert it to an exception and return */
	PyErr_SetFromErrno(PyExc_IOError);
	return NULL;
}
