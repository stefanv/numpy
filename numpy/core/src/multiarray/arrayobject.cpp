/*
  Provide multidimensional arrays as a basic object type in python.

  Based on Original Numeric implementation
  Copyright (c) 1995, 1996, 1997 Jim Hugunin, hugunin@mit.edu

  with contributions from many Numeric Python developers 1995-2004

  Heavily modified in 2005 with inspiration from Numarray

  by

  Travis Oliphant,  oliphant@ee.byu.edu
  Brigham Young Univeristy


maintainer email:  oliphant.travis@ieee.org

  Numarray design (which provided guidance) by
  Space Science Telescope Institute
  (J. Todd Miller, Perry Greenfield, Rick White)
*/
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "structmember.h"

/*#include <stdio.h>*/
#define NPY_NO_DEPRECATED_API
#define _MULTIARRAYMODULE
#define NPY_NO_PREFIX
#include "numpy/arrayobject.h"
#include "numpy/arrayscalars.h"

#include "npy_config.h"

#include "numpy/npy_3kcompat.h"

#include "common.h"

#include "number.h"
#include "usertypes.h"
#include "arraytypes.h"
#include "scalartypes.h"
#include "arrayobject.h"
#include "ctors.h"
#include "methods.h"
#include "descriptor.h"
#include "iterators.h"
#include "mapping.h"
#include "getset.h"
#include "sequence.h"
#include "buffer.h"
#include "na_object.h"
#include "na_mask.h"

/*NUMPY_API
  Compute the size of an array (in number of items)
*/
NPY_NO_EXPORT npy_intp
PyArray_Size(PyObject *op)
{
    if (PyArray_Check(op)) {
        return PyArray_SIZE((PyArrayObject *)op);
    }
    else {
        return 0;
    }
}

/*NUMPY_API
 * Sets the 'base' attribute of the array. This steals a reference
 * to 'obj'.
 *
 * Returns 0 on success, -1 on failure.
 */
NPY_NO_EXPORT int
PyArray_SetBaseObject(PyArrayObject *arr, PyObject *obj)
{
    if (obj == NULL) {
        PyErr_SetString(PyExc_ValueError,
                "Cannot set the NumPy array 'base' "
                "dependency to NULL after initialization");
        return -1;
    }
    /*
     * Allow the base to be set only once. Once the object which
     * owns the data is set, it doesn't make sense to change it.
     */
    if (PyArray_BASE(arr) != NULL) {
        Py_DECREF(obj);
        PyErr_SetString(PyExc_ValueError,
                "Cannot set the NumPy array 'base' "
                "dependency more than once");
        return -1;
    }

    /*
     * Don't allow chains of views, always set the base
     * to the owner of the data. That is, either the first object
     * which isn't an array, the first object with an NA mask
     * which owns that NA mask, or the first object which owns
     * its own data.
     */
    while (PyArray_Check(obj) && (PyObject *)arr != obj) {
        PyArrayObject *obj_arr = (PyArrayObject *)obj;
        PyObject *tmp;

        /* If this array owns its own data, stop collapsing */
        if (PyArray_CHKFLAGS(obj_arr, NPY_ARRAY_OWNDATA)) {
            break;
        }
        /*
         * If 'arr' doesn't own its NA mask, then if
         * 'obj' is NA masked and owns the mask, stop collapsing
         */
        if (!PyArray_CHKFLAGS(arr, NPY_ARRAY_OWNMASKNA) &&
                        PyArray_CHKFLAGS(obj_arr, NPY_ARRAY_OWNMASKNA)) {
            break;
        }
        /* If there's no base, stop collapsing */
        tmp = PyArray_BASE(obj_arr);
        if (tmp == NULL) {
            break;
        }

        Py_INCREF(tmp);
        Py_DECREF(obj);
        obj = tmp;
    }

    /* Disallow circular references */
    if ((PyObject *)arr == obj) {
        Py_DECREF(obj);
        PyErr_SetString(PyExc_ValueError,
                "Cannot create a circular NumPy array 'base' dependency");
        return -1;
    }

    ((PyArrayObject_fields *)arr)->base = obj;

    return 0;
}


/*NUMPY_API*/
NPY_NO_EXPORT int
PyArray_CopyObject(PyArrayObject *dest, PyObject *src_object)
{
    int ret, contains_na = 0;
    PyArrayObject *src;
    PyArray_Descr *dtype = NULL;
    int ndim = 0;
    npy_intp dims[NPY_MAXDIMS];

    Py_INCREF(src_object);
    /*
     * Special code to mimic Numeric behavior for
     * character arrays.
     */
    if (PyArray_DESCR(dest)->type == PyArray_CHARLTR &&
                                PyArray_NDIM(dest) > 0 &&
                                PyString_Check(src_object)) {
        npy_intp n_new, n_old;
        char *new_string;
        PyObject *tmp;

        n_new = PyArray_DIMS(dest)[PyArray_NDIM(dest)-1];
        n_old = PyString_Size(src_object);
        if (n_new > n_old) {
            new_string = (char *)malloc(n_new);
            memmove(new_string, PyString_AS_STRING(src_object), n_old);
            memset(new_string + n_old, ' ', n_new - n_old);
            tmp = PyString_FromStringAndSize(new_string, n_new);
            free(new_string);
            Py_DECREF(src_object);
            src_object = tmp;
        }
    }

    /*
     * Get either an array object we can copy from, or its parameters
     * if there isn't a convenient array available.
     */
    if (PyArray_GetArrayParamsFromObjectEx(src_object, PyArray_DESCR(dest),
                0, &dtype, &ndim, dims, &contains_na, &src, NULL) < 0) {
        Py_DECREF(src_object);
        return -1;
    }

    if (contains_na && !(PyArray_HasNASupport(dest) ||
                         PyArray_DESCR(dest)->type_num == NPY_OBJECT)) {
        PyErr_SetString(PyExc_ValueError,
                "Cannot set NumPy array values to NA values without first "
                "enabling NA support in the array");
        Py_DECREF(src_object);
        return -1;
    }

    /* If it's not an array, either assign from a sequence or as a scalar */
    if (src == NULL) {
        /* If the input is scalar */
        if (ndim == 0) {
            NpyNA *na = NULL;

            /* Get an NpyNA if src is NA, but don't raise an error */
            if (PyArray_HasNASupport(dest)) {
                na = NpyNA_FromObject(src_object, 1);
            }

            /* If there's one dest element and src is a Python scalar */
            if (PyArray_IsScalar(src_object, Generic)) {
                char *value;
                int retcode;

                value = (char *)scalar_value(src_object, dtype);
                if (value == NULL) {
                    Py_DECREF(dtype);
                    Py_DECREF(src_object);
                    return -1;
                }

                /* TODO: switch to SAME_KIND casting */
                retcode = PyArray_AssignRawScalar(dest, dtype, value,
                                        NULL, NPY_UNSAFE_CASTING, 0, NULL);
                Py_DECREF(dtype);
                Py_DECREF(src_object);
                return retcode;
            }
            /* Assigning NA affects the mask if it exists */
            else if (na != NULL) {
                if (PyArray_AssignNA(dest, na, NULL, 0, NULL) < 0) {
                    Py_DECREF(dtype);
                    Py_DECREF(na);
                    Py_DECREF(src_object);
                    return -1;
                }

                Py_DECREF(na);
                Py_DECREF(src_object);
                return 0;
            }
            /* Otherwise use the dtype's setitem function */
            else {
                if (PyArray_SIZE(dest) == 1) {
                    Py_DECREF(dtype);
                    Py_DECREF(src_object);
                    ret = PyArray_DESCR(dest)->f->setitem(src_object,
                                                PyArray_DATA(dest), dest);
                    /* Unmask the value if necessary */
                    if (ret == 0 && PyArray_HASMASKNA(dest)) {
                        PyArray_MASKNA_DATA(dest)[0] = 1;
                    }
                    return ret;
                }
                else {
                    src = (PyArrayObject *)PyArray_NewFromDescr(&PyArray_Type,
                                                        dtype, 0, NULL, NULL,
                                                        NULL, 0, NULL);
                    if (src == NULL) {
                        Py_DECREF(src_object);
                        return -1;
                    }
                    if (PyArray_DESCR(src)->f->setitem(src_object,
                                                PyArray_DATA(src), src) < 0) {
                        Py_DECREF(src_object);
                        Py_DECREF(src);
                        return -1;
                    }
                }
            }
        }
        else {
            /*
             * If there are more than enough dims, use AssignFromSequence
             * because it can handle this style of broadcasting.
             */
            if (ndim >= PyArray_NDIM(dest)) {
                int res;
                Py_DECREF(dtype);
                res = PyArray_AssignFromSequence(dest, src_object);
                Py_DECREF(src_object);
                return res;
            }
            /* Otherwise convert to an array and do an array-based copy */
            src = (PyArrayObject *)PyArray_NewFromDescr(&PyArray_Type,
                                        dtype, ndim, dims, NULL, NULL,
                                        PyArray_ISFORTRAN(dest), NULL);
            if (src == NULL) {
                Py_DECREF(src_object);
                return -1;
            }
            if (PyArray_HASMASKNA(dest)) {
                if (PyArray_AllocateMaskNA(dest, 1, 0, 1) < 0) {
                    Py_DECREF(src_object);
                    return -1;
                }
            }
            if (PyArray_AssignFromSequence(src, src_object) < 0) {
                Py_DECREF(src);
                Py_DECREF(src_object);
                return -1;
            }
        }
    }

    /* If it's an array, do a move (handling possible overlapping data) */
    ret = PyArray_MoveInto(dest, src);
    Py_DECREF(src);
    Py_DECREF(src_object);
    return ret;
}


/* returns an Array-Scalar Object of the type of arr
   from the given pointer to memory -- main Scalar creation function
   default new method calls this.
*/

/* Ideally, here the descriptor would contain all the information needed.
   So, that we simply need the data and the descriptor, and perhaps
   a flag
*/


/*
  Given a string return the type-number for
  the data-type with that string as the type-object name.
  Returns PyArray_NOTYPE without setting an error if no type can be
  found.  Only works for user-defined data-types.
*/

/*NUMPY_API
 */
NPY_NO_EXPORT int
PyArray_TypeNumFromName(char *str)
{
    int i;
    PyArray_Descr *descr;

    for (i = 0; i < NPY_NUMUSERTYPES; i++) {
        descr = userdescrs[i];
        if (strcmp(descr->typeobj->tp_name, str) == 0) {
            return descr->type_num;
        }
    }
    return PyArray_NOTYPE;
}

/*********************** end C-API functions **********************/

/* array object functions */

static void
array_dealloc(PyArrayObject *self)
{
    PyArrayObject_fields *fa = (PyArrayObject_fields *)self;

    _array_dealloc_buffer_info(self);

    if (fa->weakreflist != NULL) {
        PyObject_ClearWeakRefs((PyObject *)self);
    }
    if (fa->base) {
        /*
         * UPDATEIFCOPY means that base points to an
         * array that should be updated with the contents
         * of this array upon destruction.
         * fa->base->flags must have been WRITEABLE
         * (checked previously) and it was locked here
         * thus, unlock it.
         */
        if (fa->flags & NPY_ARRAY_UPDATEIFCOPY) {
            PyArray_ENABLEFLAGS(((PyArrayObject *)fa->base),
                                                    NPY_ARRAY_WRITEABLE);
            Py_INCREF(self); /* hold on to self in next call */
            if (PyArray_CopyAnyInto((PyArrayObject *)fa->base, self) < 0) {
                PyErr_Print();
                PyErr_Clear();
            }
            /*
             * Don't need to DECREF -- because we are deleting
             *self already...
             */
        }
        /*
         * In any case base is pointing to something that we need
         * to DECREF -- either a view or a buffer object
         */
        Py_DECREF(fa->base);
    }

    if ((fa->flags & NPY_ARRAY_OWNDATA) && fa->data) {
        /* Free internal references if an Object array */
        if (PyDataType_FLAGCHK(fa->descr, NPY_ITEM_REFCOUNT)) {
            Py_INCREF(self); /*hold on to self */
            PyArray_XDECREF(self);
            /*
             * Don't need to DECREF -- because we are deleting
             * self already...
             */
        }
        PyDataMem_FREE(fa->data);
    }

    /* If the array has an NA mask, free its associated data */
    if (fa->flags & NPY_ARRAY_MASKNA) {
        Py_XDECREF(fa->maskna_dtype);
        if (fa->flags & NPY_ARRAY_OWNMASKNA) {
            PyDataMem_FREE(fa->maskna_data);
        }
    }

    PyDimMem_FREE(fa->dimensions);
    Py_DECREF(fa->descr);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static int
dump_data(char **string, int *n, int *max_n, char *data, int nd,
          npy_intp *dimensions, npy_intp *strides, PyArrayObject* self)
{
    PyArray_Descr *descr=PyArray_DESCR(self);
    PyObject *op, *sp;
    char *ostring;
    npy_intp i, N;

#define CHECK_MEMORY do { if (*n >= *max_n-16) {         \
        *max_n *= 2;                                     \
        *string = (char *)_pya_realloc(*string, *max_n); \
    }} while (0)

    if (nd == 0) {
        if ((op = descr->f->getitem(data, self)) == NULL) {
            return -1;
        }
        sp = PyObject_Repr(op);
        if (sp == NULL) {
            Py_DECREF(op);
            return -1;
        }
        ostring = PyString_AsString(sp);
        N = PyString_Size(sp)*sizeof(char);
        *n += N;
        CHECK_MEMORY;
        memmove(*string + (*n - N), ostring, N);
        Py_DECREF(sp);
        Py_DECREF(op);
        return 0;
    }
    else {
        CHECK_MEMORY;
        (*string)[*n] = '[';
        *n += 1;
        for (i = 0; i < dimensions[0]; i++) {
            if (dump_data(string, n, max_n,
                          data + (*strides)*i,
                          nd - 1, dimensions + 1,
                          strides + 1, self) < 0) {
                return -1;
            }
            CHECK_MEMORY;
            if (i < dimensions[0] - 1) {
                (*string)[*n] = ',';
                (*string)[*n+1] = ' ';
                *n += 2;
            }
        }
        CHECK_MEMORY;
        (*string)[*n] = ']';
        *n += 1;
        return 0;
    }

#undef CHECK_MEMORY
}

/*NUMPY_API
 * Prints the raw data of the ndarray in a form useful for debugging
 * low-level C issues.
 */
NPY_NO_EXPORT void
PyArray_DebugPrint(PyArrayObject *obj)
{
    int i;
    PyArrayObject_fields *fobj = (PyArrayObject_fields *)obj;

    printf("-------------------------------------------------------\n");
    printf(" Dump of NumPy ndarray at address %p\n", obj);
    if (obj == NULL) {
        printf(" It's NULL!\n");
        printf("-------------------------------------------------------\n");
        fflush(stdout);
        return;
    }
    printf(" ndim   : %d\n", fobj->nd);
    printf(" shape  :");
    for (i = 0; i < fobj->nd; ++i) {
        printf(" %d", (int)fobj->dimensions[i]);
    }
    printf("\n");

    printf(" dtype  : ");
    PyObject_Print((PyObject *)fobj->descr, stdout, 0);
    printf("\n");
    printf(" data   : %p\n", fobj->data);
    printf(" strides:");
    for (i = 0; i < fobj->nd; ++i) {
        printf(" %d", (int)fobj->strides[i]);
    }
    printf("\n");

    printf(" base   : %p\n", fobj->base);

    printf(" flags :");
    if (fobj->flags & NPY_ARRAY_C_CONTIGUOUS)
        printf(" C_CONTIGUOUS");
    if (fobj->flags & NPY_ARRAY_F_CONTIGUOUS)
        printf(" F_CONTIGUOUS");
    if (fobj->flags & NPY_ARRAY_OWNDATA)
        printf(" OWNDATA");
    if (fobj->flags & NPY_ARRAY_ALIGNED)
        printf(" ALIGNED");
    if (fobj->flags & NPY_ARRAY_WRITEABLE)
        printf(" WRITEABLE");
    if (fobj->flags & NPY_ARRAY_UPDATEIFCOPY)
        printf(" UPDATEIFCOPY");
    if (fobj->flags & NPY_ARRAY_MASKNA)
        printf(" MASKNA");
    if (fobj->flags & NPY_ARRAY_OWNMASKNA)
        printf(" OWNMASKNA");
    printf("\n");

    if (fobj->flags & NPY_ARRAY_MASKNA) {
        printf(" maskna dtype  : ");
        PyObject_Print((PyObject *)fobj->maskna_dtype, stdout, 0);
        printf("\n");
        printf(" maskna data   : %p\n", fobj->maskna_data);
        printf(" maskna strides:");
        for (i = 0; i < fobj->nd; ++i) {
            printf(" %d", (int)fobj->maskna_strides[i]);
        }
        printf("\n");
    }

    if (fobj->base != NULL && PyArray_Check(fobj->base)) {
        printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
        printf("Dump of array's BASE:\n");
        PyArray_DebugPrint((PyArrayObject *)fobj->base);
        printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
    }
    printf("-------------------------------------------------------\n");
    fflush(stdout);
}

static PyObject *
array_repr_builtin(PyArrayObject *self, int repr)
{
    PyObject *ret;
    char *string;
    int n, max_n;

    max_n = PyArray_NBYTES(self)*4*sizeof(char) + 7;

    if ((string = (char *)_pya_malloc(max_n)) == NULL) {
        return PyErr_NoMemory();
    }

    if (repr) {
        n = 6;
        sprintf(string, "array(");
    }
    else {
        n = 0;
    }
    if (dump_data(&string, &n, &max_n, PyArray_DATA(self),
                  PyArray_NDIM(self), PyArray_DIMS(self),
                  PyArray_STRIDES(self), self) < 0) {
        _pya_free(string);
        return NULL;
    }

    if (repr) {
        if (PyArray_ISEXTENDED(self)) {
            char buf[100];
            PyOS_snprintf(buf, sizeof(buf), "%d", PyArray_DESCR(self)->elsize);
            sprintf(string+n, ", '%c%s')", PyArray_DESCR(self)->type, buf);
            ret = PyUString_FromStringAndSize(string, n + 6 + strlen(buf));
        }
        else {
            sprintf(string+n, ", '%c')", PyArray_DESCR(self)->type);
            ret = PyUString_FromStringAndSize(string, n+6);
        }
    }
    else {
        ret = PyUString_FromStringAndSize(string, n);
    }

    _pya_free(string);
    return ret;
}

static PyObject *PyArray_StrFunction = NULL;
static PyObject *PyArray_ReprFunction = NULL;

/*NUMPY_API
 * Set the array print function to be a Python function.
 */
NPY_NO_EXPORT void
PyArray_SetStringFunction(PyObject *op, int repr)
{
    if (repr) {
        /* Dispose of previous callback */
        Py_XDECREF(PyArray_ReprFunction);
        /* Add a reference to new callback */
        Py_XINCREF(op);
        /* Remember new callback */
        PyArray_ReprFunction = op;
    }
    else {
        /* Dispose of previous callback */
        Py_XDECREF(PyArray_StrFunction);
        /* Add a reference to new callback */
        Py_XINCREF(op);
        /* Remember new callback */
        PyArray_StrFunction = op;
    }
}

/*NUMPY_API
 * This function is scheduled to be removed
 *
 * TO BE REMOVED - NOT USED INTERNALLY.
 */
NPY_NO_EXPORT void
PyArray_SetDatetimeParseFunction(PyObject *op)
{
}


static PyObject *
array_repr(PyArrayObject *self)
{
    PyObject *s, *arglist;

    if (PyArray_ReprFunction == NULL) {
        s = array_repr_builtin(self, 1);
    }
    else {
        arglist = Py_BuildValue("(O)", self);
        s = PyEval_CallObject(PyArray_ReprFunction, arglist);
        Py_DECREF(arglist);
    }
    return s;
}

static PyObject *
array_str(PyArrayObject *self)
{
    PyObject *s, *arglist;

    if (PyArray_StrFunction == NULL) {
        s = array_repr_builtin(self, 0);
    }
    else {
        arglist = Py_BuildValue("(O)", self);
        s = PyEval_CallObject(PyArray_StrFunction, arglist);
        Py_DECREF(arglist);
    }
    return s;
}



/*NUMPY_API
 */
NPY_NO_EXPORT int
PyArray_CompareUCS4(npy_ucs4 *s1, npy_ucs4 *s2, size_t len)
{
    PyArray_UCS4 c1, c2;
    while(len-- > 0) {
        c1 = *s1++;
        c2 = *s2++;
        if (c1 != c2) {
            return (c1 < c2) ? -1 : 1;
        }
    }
    return 0;
}

/*NUMPY_API
 */
NPY_NO_EXPORT int
PyArray_CompareString(char *s1, char *s2, size_t len)
{
    const unsigned char *c1 = (unsigned char *)s1;
    const unsigned char *c2 = (unsigned char *)s2;
    size_t i;

    for(i = 0; i < len; ++i) {
        if (c1[i] != c2[i]) {
            return (c1[i] > c2[i]) ? 1 : -1;
        }
    }
    return 0;
}


/* This also handles possibly mis-aligned data */
/* Compare s1 and s2 which are not necessarily NULL-terminated.
   s1 is of length len1
   s2 is of length len2
   If they are NULL terminated, then stop comparison.
*/
static int
_myunincmp(PyArray_UCS4 *s1, PyArray_UCS4 *s2, int len1, int len2)
{
    PyArray_UCS4 *sptr;
    PyArray_UCS4 *s1t=s1, *s2t=s2;
    int val;
    npy_intp size;
    int diff;

    if ((npy_intp)s1 % sizeof(PyArray_UCS4) != 0) {
        size = len1*sizeof(PyArray_UCS4);
        s1t = (npy_ucs4 *)malloc(size);
        memcpy(s1t, s1, size);
    }
    if ((npy_intp)s2 % sizeof(PyArray_UCS4) != 0) {
        size = len2*sizeof(PyArray_UCS4);
        s2t = (npy_ucs4 *)malloc(size);
        memcpy(s2t, s2, size);
    }
    val = PyArray_CompareUCS4(s1t, s2t, MIN(len1,len2));
    if ((val != 0) || (len1 == len2)) {
        goto finish;
    }
    if (len2 > len1) {
        sptr = s2t+len1;
        val = -1;
        diff = len2-len1;
    }
    else {
        sptr = s1t+len2;
        val = 1;
        diff=len1-len2;
    }
    while (diff--) {
        if (*sptr != 0) {
            goto finish;
        }
        sptr++;
    }
    val = 0;

 finish:
    if (s1t != s1) {
        free(s1t);
    }
    if (s2t != s2) {
        free(s2t);
    }
    return val;
}




/*
 * Compare s1 and s2 which are not necessarily NULL-terminated.
 * s1 is of length len1
 * s2 is of length len2
 * If they are NULL terminated, then stop comparison.
 */
static int
_mystrncmp(char *s1, char *s2, int len1, int len2)
{
    char *sptr;
    int val;
    int diff;

    val = memcmp(s1, s2, MIN(len1, len2));
    if ((val != 0) || (len1 == len2)) {
        return val;
    }
    if (len2 > len1) {
        sptr = s2 + len1;
        val = -1;
        diff = len2 - len1;
    }
    else {
        sptr = s1 + len2;
        val = 1;
        diff = len1 - len2;
    }
    while (diff--) {
        if (*sptr != 0) {
            return val;
        }
        sptr++;
    }
    return 0; /* Only happens if NULLs are everywhere */
}

/* Borrowed from Numarray */

#define SMALL_STRING 2048

#if defined(isspace)
#undef isspace
#define isspace(c)  ((c==' ')||(c=='\t')||(c=='\n')||(c=='\r')||(c=='\v')||(c=='\f'))
#endif

static void _rstripw(char *s, int n)
{
    int i;
    for (i = n - 1; i >= 1; i--) { /* Never strip to length 0. */
        int c = s[i];

        if (!c || isspace(c)) {
            s[i] = 0;
        }
        else {
            break;
        }
    }
}

static void _unistripw(PyArray_UCS4 *s, int n)
{
    int i;
    for (i = n - 1; i >= 1; i--) { /* Never strip to length 0. */
        PyArray_UCS4 c = s[i];
        if (!c || isspace(c)) {
            s[i] = 0;
        }
        else {
            break;
        }
    }
}


static char *
_char_copy_n_strip(char *original, char *temp, int nc)
{
    if (nc > SMALL_STRING) {
        temp = (char *)malloc(nc);
        if (!temp) {
            PyErr_NoMemory();
            return NULL;
        }
    }
    memcpy(temp, original, nc);
    _rstripw(temp, nc);
    return temp;
}

static void
_char_release(char *ptr, int nc)
{
    if (nc > SMALL_STRING) {
        free(ptr);
    }
}

static char *
_uni_copy_n_strip(char *original, char *temp, int nc)
{
    if (nc*sizeof(PyArray_UCS4) > SMALL_STRING) {
        temp = (char *)malloc(nc*sizeof(PyArray_UCS4));
        if (!temp) {
            PyErr_NoMemory();
            return NULL;
        }
    }
    memcpy(temp, original, nc*sizeof(PyArray_UCS4));
    _unistripw((PyArray_UCS4 *)temp, nc);
    return temp;
}

static void
_uni_release(char *ptr, int nc)
{
    if (nc*sizeof(PyArray_UCS4) > SMALL_STRING) {
        free(ptr);
    }
}


/* End borrowed from numarray */

#define _rstrip_loop(CMP) {                                     \
        void *aptr, *bptr;                                      \
        char atemp[SMALL_STRING], btemp[SMALL_STRING];          \
        while(size--) {                                         \
            aptr = stripfunc(iself->dataptr, atemp, N1);        \
            if (!aptr) return -1;                               \
            bptr = stripfunc(iother->dataptr, btemp, N2);       \
            if (!bptr) {                                        \
                relfunc((char *)aptr, N1);                      \
                return -1;                                      \
            }                                                   \
            val = compfunc(aptr, bptr, N1, N2);                 \
            *dptr = (val CMP 0);                                \
            PyArray_ITER_NEXT(iself);                           \
            PyArray_ITER_NEXT(iother);                          \
            dptr += 1;                                          \
            relfunc((char *)aptr, N1);                          \
            relfunc((char *)bptr, N2);                          \
        }                                                       \
    }

#define _reg_loop(CMP) {                                \
        while(size--) {                                 \
            val = compfunc((void *)iself->dataptr,      \
                          (void *)iother->dataptr,      \
                          N1, N2);                      \
            *dptr = (val CMP 0);                        \
            PyArray_ITER_NEXT(iself);                   \
            PyArray_ITER_NEXT(iother);                  \
            dptr += 1;                                  \
        }                                               \
    }

static int
_compare_strings(PyArrayObject *result, PyArrayMultiIterObject *multi,
                 int cmp_op, void *func, int rstrip)
{
    PyArrayIterObject *iself, *iother;
    Bool *dptr;
    npy_intp size;
    int val;
    int N1, N2;
    int (*compfunc)(void *, void *, int, int);
    void (*relfunc)(char *, int);
    char* (*stripfunc)(char *, char *, int);

    compfunc = (int (*)(void*, void*, int, int))func;
    dptr = (Bool *)PyArray_DATA(result);
    iself = multi->iters[0];
    iother = multi->iters[1];
    size = multi->size;
    N1 = PyArray_DESCR(iself->ao)->elsize;
    N2 = PyArray_DESCR(iother->ao)->elsize;
    if ((void *)compfunc == (void *)_myunincmp) {
        N1 >>= 2;
        N2 >>= 2;
        stripfunc = _uni_copy_n_strip;
        relfunc = _uni_release;
    }
    else {
        stripfunc = _char_copy_n_strip;
        relfunc = _char_release;
    }
    switch (cmp_op) {
    case Py_EQ:
        if (rstrip) {
            _rstrip_loop(==);
        } else {
            _reg_loop(==);
        }
        break;
    case Py_NE:
        if (rstrip) {
            _rstrip_loop(!=);
        } else {
            _reg_loop(!=);
        }
        break;
    case Py_LT:
        if (rstrip) {
            _rstrip_loop(<);
        } else {
            _reg_loop(<);
        }
        break;
    case Py_LE:
        if (rstrip) {
            _rstrip_loop(<=);
        } else {
            _reg_loop(<=);
        }
        break;
    case Py_GT:
        if (rstrip) {
            _rstrip_loop(>);
        } else {
            _reg_loop(>);
        }
        break;
    case Py_GE:
        if (rstrip) {
            _rstrip_loop(>=);
        } else {
            _reg_loop(>=);
        }
        break;
    default:
        PyErr_SetString(PyExc_RuntimeError, "bad comparison operator");
        return -1;
    }
    return 0;
}

#undef _reg_loop
#undef _rstrip_loop
#undef SMALL_STRING

NPY_NO_EXPORT PyObject *
_strings_richcompare(PyArrayObject *self, PyArrayObject *other, int cmp_op,
                     int rstrip)
{
    PyArrayObject *result;
    PyArrayMultiIterObject *mit;
    int val;

    /* Cast arrays to a common type */
    if (PyArray_TYPE(self) != PyArray_DESCR(other)->type_num) {
#if defined(NPY_PY3K)
        /*
         * Comparison between Bytes and Unicode is not defined in Py3K;
         * we follow.
         */
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
#else
        PyObject *newObj;
        if (PyArray_TYPE(self) == PyArray_STRING &&
            PyArray_DESCR(other)->type_num == PyArray_UNICODE) {
            PyArray_Descr* unicode = PyArray_DescrNew(PyArray_DESCR(other));
            unicode->elsize = PyArray_DESCR(self)->elsize << 2;
            newObj = PyArray_FromAny((PyObject *)self, unicode,
                                      0, 0, 0, NULL);
            if (newObj == NULL) {
                return NULL;
            }
            Py_INCREF(other);
            self = (PyArrayObject *)newObj;
        }
        else if (PyArray_TYPE(self) == PyArray_UNICODE &&
                 PyArray_DESCR(other)->type_num == PyArray_STRING) {
            PyArray_Descr* unicode = PyArray_DescrNew(PyArray_DESCR(self));
            unicode->elsize = PyArray_DESCR(other)->elsize << 2;
            newObj = PyArray_FromAny((PyObject *)other, unicode,
                                      0, 0, 0, NULL);
            if (newObj == NULL) {
                return NULL;
            }
            Py_INCREF(self);
            other = (PyArrayObject *)newObj;
        }
        else {
            PyErr_SetString(PyExc_TypeError,
                            "invalid string data-types "
                            "in comparison");
            return NULL;
        }
#endif
    }
    else {
        Py_INCREF(self);
        Py_INCREF(other);
    }

    /* Broad-cast the arrays to a common shape */
    mit = (PyArrayMultiIterObject *)PyArray_MultiIterNew(2, self, other);
    Py_DECREF(self);
    Py_DECREF(other);
    if (mit == NULL) {
        return NULL;
    }

    result = (PyArrayObject *)PyArray_NewFromDescr(&PyArray_Type,
                                  PyArray_DescrFromType(PyArray_BOOL),
                                  mit->nd,
                                  mit->dimensions,
                                  NULL, NULL, 0,
                                  NULL);
    if (result == NULL) {
        goto finish;
    }

    if (PyArray_TYPE(self) == NPY_UNICODE) {
        val = _compare_strings(result, mit, cmp_op, (void *)_myunincmp, rstrip);
    }
    else {
        val = _compare_strings(result, mit, cmp_op, (void *)_mystrncmp, rstrip);
    }

    if (val < 0) {
        Py_DECREF(result);
        result = NULL;
    }

 finish:
    Py_DECREF(mit);
    return (PyObject *)result;
}

/*
 * VOID-type arrays can only be compared equal and not-equal
 * in which case the fields are all compared by extracting the fields
 * and testing one at a time...
 * equality testing is performed using logical_ands on all the fields.
 * in-equality testing is performed using logical_ors on all the fields.
 *
 * VOID-type arrays without fields are compared for equality by comparing their
 * memory at each location directly (using string-code).
 */
static PyObject *
_void_compare(PyArrayObject *self, PyArrayObject *other, int cmp_op)
{
    if (!(cmp_op == Py_EQ || cmp_op == Py_NE)) {
        PyErr_SetString(PyExc_ValueError,
                "Void-arrays can only be compared for equality.");
        return NULL;
    }
    if (PyArray_HASFIELDS(self)) {
        PyObject *res = NULL, *temp, *a, *b;
        PyObject *key, *value, *temp2;
        PyObject *op;
        Py_ssize_t pos = 0;
        npy_intp result_ndim = PyArray_NDIM(self) > PyArray_NDIM(other) ?
                            PyArray_NDIM(self) : PyArray_NDIM(other);

        op = (cmp_op == Py_EQ ? n_ops.logical_and : n_ops.logical_or);
        while (PyDict_Next(PyArray_DESCR(self)->fields, &pos, &key, &value)) {
            if NPY_TITLE_KEY(key, value) {
                continue;
            }
            a = PyArray_EnsureAnyArray(array_subscript(self, key));
            if (a == NULL) {
                Py_XDECREF(res);
                return NULL;
            }
            b = array_subscript(other, key);
            if (b == NULL) {
                Py_XDECREF(res);
                Py_DECREF(a);
                return NULL;
            }
            temp = array_richcompare((PyArrayObject *)a,b,cmp_op);
            Py_DECREF(a);
            Py_DECREF(b);
            if (temp == NULL) {
                Py_XDECREF(res);
                return NULL;
            }

            /*
             * If the field type has a non-trivial shape, additional
             * dimensions will have been appended to `a` and `b`.
             * In that case, reduce them using `op`.
             */
            if (PyArray_Check(temp) &&
                        PyArray_NDIM((PyArrayObject *)temp) > result_ndim) {
                /* If the type was multidimensional, collapse that part to 1-D
                 */
                if (PyArray_NDIM((PyArrayObject *)temp) != result_ndim+1) {
                    npy_intp dimensions[NPY_MAXDIMS];
                    PyArray_Dims newdims;

                    newdims.ptr = dimensions;
                    newdims.len = result_ndim+1;
                    memcpy(dimensions, PyArray_DIMS((PyArrayObject *)temp),
                           sizeof(intp)*result_ndim);
                    dimensions[result_ndim] = -1;
                    temp2 = PyArray_Newshape((PyArrayObject *)temp,
                                             &newdims, PyArray_ANYORDER);
                    if (temp2 == NULL) {
                        Py_DECREF(temp);
                        Py_XDECREF(res);
                        return NULL;
                    }
                    Py_DECREF(temp);
                    temp = temp2;
                }
                /* Reduce the extra dimension of `temp` using `op` */
                temp2 = PyArray_GenericReduceFunction((PyArrayObject *)temp,
                                                      op, result_ndim,
                                                      PyArray_BOOL, NULL);
                if (temp2 == NULL) {
                    Py_DECREF(temp);
                    Py_XDECREF(res);
                    return NULL;
                }
                Py_DECREF(temp);
                temp = temp2;
            }

            if (res == NULL) {
                res = temp;
            }
            else {
                temp2 = PyObject_CallFunction(op, "OO", res, temp);
                Py_DECREF(temp);
                Py_DECREF(res);
                if (temp2 == NULL) {
                    return NULL;
                }
                res = temp2;
            }
        }
        if (res == NULL && !PyErr_Occurred()) {
            PyErr_SetString(PyExc_ValueError, "No fields found.");
        }
        return res;
    }
    else {
        /*
         * compare as a string. Assumes self and
         * other have same descr->type
         */
        return _strings_richcompare(self, other, cmp_op, 0);
    }
}

NPY_NO_EXPORT PyObject *
array_richcompare(PyArrayObject *self, PyObject *other, int cmp_op)
{
    PyArrayObject *array_other;
    PyObject *result = NULL;
    PyArray_Descr *dtype = NULL;

    switch (cmp_op) {
    case Py_LT:
        result = PyArray_GenericBinaryFunction(self, other,
                n_ops.less);
        break;
    case Py_LE:
        result = PyArray_GenericBinaryFunction(self, other,
                n_ops.less_equal);
        break;
    case Py_EQ:
        if (other == Py_None) {
            Py_INCREF(Py_False);
            return Py_False;
        }
        /* Make sure 'other' is an array */
        if (PyArray_TYPE(self) == NPY_OBJECT) {
            dtype = PyArray_DTYPE(self);
            Py_INCREF(dtype);
        }
        array_other = (PyArrayObject *)PyArray_FromAny(other, dtype, 0, 0,
                                                    NPY_ARRAY_ALLOWNA, NULL);
        /*
         * If not successful, indicate that the items cannot be compared
         * this way.
         */
        if (array_other == NULL) {
            PyErr_Clear();
            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }

        result = PyArray_GenericBinaryFunction(self,
                (PyObject *)array_other,
                n_ops.equal);
        if ((result == Py_NotImplemented) &&
                (PyArray_TYPE(self) == NPY_VOID)) {
            int _res;

            _res = PyObject_RichCompareBool
                ((PyObject *)PyArray_DESCR(self),
                 (PyObject *)PyArray_DESCR(array_other),
                 Py_EQ);
            if (_res < 0) {
                Py_DECREF(result);
                Py_DECREF(array_other);
                return NULL;
            }
            if (_res) {
                Py_DECREF(result);
                result = _void_compare(self, array_other, cmp_op);
            }
            Py_DECREF(array_other);
            return result;
        }
        /*
         * If the comparison results in NULL, then the
         * two array objects can not be compared together;
         * indicate that
         */
        Py_DECREF(array_other);
        if (result == NULL) {
            PyErr_Clear();
            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }
        break;
    case Py_NE:
        if (other == Py_None) {
            Py_INCREF(Py_True);
            return Py_True;
        }
        /* Make sure 'other' is an array */
        if (PyArray_TYPE(self) == NPY_OBJECT) {
            dtype = PyArray_DTYPE(self);
            Py_INCREF(dtype);
        }
        array_other = (PyArrayObject *)PyArray_FromAny(other, dtype, 0, 0,
                                                    NPY_ARRAY_ALLOWNA, NULL);
        /*
         * If not successful, indicate that the items cannot be compared
         * this way.
         */
        if (array_other == NULL) {
            PyErr_Clear();
            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }

        result = PyArray_GenericBinaryFunction(self, (PyObject *)array_other,
                n_ops.not_equal);
        if ((result == Py_NotImplemented) &&
                (PyArray_TYPE(self) == NPY_VOID)) {
            int _res;

            _res = PyObject_RichCompareBool(
                    (PyObject *)PyArray_DESCR(self),
                    (PyObject *)PyArray_DESCR(array_other),
                    Py_EQ);
            if (_res < 0) {
                Py_DECREF(result);
                Py_DECREF(array_other);
                return NULL;
            }
            if (_res) {
                Py_DECREF(result);
                result = _void_compare(self, array_other, cmp_op);
                Py_DECREF(array_other);
            }
            return result;
        }

        Py_DECREF(array_other);
        if (result == NULL) {
            PyErr_Clear();
            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }
        break;
    case Py_GT:
        result = PyArray_GenericBinaryFunction(self, other,
                n_ops.greater);
        break;
    case Py_GE:
        result = PyArray_GenericBinaryFunction(self, other,
                n_ops.greater_equal);
        break;
    default:
        result = Py_NotImplemented;
        Py_INCREF(result);
    }
    if (result == Py_NotImplemented) {
        /* Try to handle string comparisons */
        if (PyArray_TYPE(self) == PyArray_OBJECT) {
            return result;
        }
        array_other = (PyArrayObject *)PyArray_FromObject(other,
                                                    PyArray_NOTYPE, 0, 0);
        if (PyArray_ISSTRING(self) && PyArray_ISSTRING(array_other)) {
            Py_DECREF(result);
            result = _strings_richcompare(self, (PyArrayObject *)
                                          array_other, cmp_op, 0);
        }
        Py_DECREF(array_other);
    }
    return result;
}

/*NUMPY_API
 */
NPY_NO_EXPORT int
PyArray_ElementStrides(PyObject *obj)
{
    PyArrayObject *arr;
    int itemsize;
    int i, ndim;
    npy_intp *strides;

    if (!PyArray_Check(obj)) {
        return 0;
    }

    arr = (PyArrayObject *)obj;

    itemsize = PyArray_ITEMSIZE(arr);
    ndim = PyArray_NDIM(arr);
    strides = PyArray_STRIDES(arr);

    for (i = 0; i < ndim; i++) {
        if ((strides[i] % itemsize) != 0) {
            return 0;
        }
    }
    return 1;
}

/*
 * This routine checks to see if newstrides (of length nd) will not
 * ever be able to walk outside of the memory implied numbytes and offset.
 *
 * The available memory is assumed to start at -offset and proceed
 * to numbytes-offset.  The strides are checked to ensure
 * that accessing memory using striding will not try to reach beyond
 * this memory for any of the axes.
 *
 * If numbytes is 0 it will be calculated using the dimensions and
 * element-size.
 *
 * This function checks for walking beyond the beginning and right-end
 * of the buffer and therefore works for any integer stride (positive
 * or negative).
 */

/*NUMPY_API*/
NPY_NO_EXPORT Bool
PyArray_CheckStrides(int elsize, int nd, npy_intp numbytes, npy_intp offset,
                     npy_intp *dims, npy_intp *newstrides)
{
    int i;
    npy_intp byte_begin;
    npy_intp begin;
    npy_intp end;

    if (numbytes == 0) {
        numbytes = PyArray_MultiplyList(dims, nd) * elsize;
    }
    begin = -offset;
    end = numbytes - offset - elsize;
    for (i = 0; i < nd; i++) {
        byte_begin = newstrides[i]*(dims[i] - 1);
        if ((byte_begin < begin) || (byte_begin > end)) {
            return FALSE;
        }
    }
    return TRUE;
}


static PyObject *
array_new(PyTypeObject *subtype, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"shape", "dtype", "buffer", "offset", "strides",
                             "order", NULL};
    PyArray_Descr *descr = NULL;
    int itemsize;
    PyArray_Dims dims = {NULL, 0};
    PyArray_Dims strides = {NULL, 0};
    PyArray_Chunk buffer;
    longlong offset = 0;
    NPY_ORDER order = NPY_CORDER;
    int is_f_order = 0;
    PyArrayObject *ret;

    buffer.ptr = NULL;
    /*
     * Usually called with shape and type but can also be called with buffer,
     * strides, and swapped info For now, let's just use this to create an
     * empty, contiguous array of a specific type and shape.
     */
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&|O&O&LO&O&",
                                     kwlist, PyArray_IntpConverter,
                                     &dims,
                                     PyArray_DescrConverter,
                                     &descr,
                                     PyArray_BufferConverter,
                                     &buffer,
                                     &offset,
                                     &PyArray_IntpConverter,
                                     &strides,
                                     &PyArray_OrderConverter,
                                     &order)) {
        goto fail;
    }
    if (order == NPY_FORTRANORDER) {
        is_f_order = 1;
    }
    if (descr == NULL) {
        descr = PyArray_DescrFromType(NPY_DEFAULT_TYPE);
    }

    itemsize = descr->elsize;
    if (itemsize == 0) {
        PyErr_SetString(PyExc_ValueError,
                        "data-type with unspecified variable length");
        goto fail;
    }

    if (strides.ptr != NULL) {
        npy_intp nb, off;
        if (strides.len != dims.len) {
            PyErr_SetString(PyExc_ValueError,
                            "strides, if given, must be "   \
                            "the same length as shape");
            goto fail;
        }

        if (buffer.ptr == NULL) {
            nb = 0;
            off = 0;
        }
        else {
            nb = buffer.len;
            off = (npy_intp) offset;
        }


        if (!PyArray_CheckStrides(itemsize, dims.len,
                                  nb, off,
                                  dims.ptr, strides.ptr)) {
            PyErr_SetString(PyExc_ValueError,
                            "strides is incompatible "      \
                            "with shape of requested "      \
                            "array and size of buffer");
            goto fail;
        }
    }

    if (buffer.ptr == NULL) {
        ret = (PyArrayObject *)
            PyArray_NewFromDescr(subtype, descr,
                                 (int)dims.len,
                                 dims.ptr,
                                 strides.ptr, NULL, is_f_order, NULL);
        if (ret == NULL) {
            descr = NULL;
            goto fail;
        }
        if (PyDataType_FLAGCHK(descr, NPY_ITEM_HASOBJECT)) {
            /* place Py_None in object positions */
            PyArray_FillObjectArray(ret, Py_None);
            if (PyErr_Occurred()) {
                descr = NULL;
                goto fail;
            }
        }
    }
    else {
        /* buffer given -- use it */
        if (dims.len == 1 && dims.ptr[0] == -1) {
            dims.ptr[0] = (buffer.len-(npy_intp)offset) / itemsize;
        }
        else if ((strides.ptr == NULL) &&
                 (buffer.len < (offset + (((npy_intp)itemsize)*
                                          PyArray_MultiplyList(dims.ptr,
                                                               dims.len))))) {
            PyErr_SetString(PyExc_TypeError,
                            "buffer is too small for "      \
                            "requested array");
            goto fail;
        }
        /* get writeable and aligned */
        if (is_f_order) {
            buffer.flags |= NPY_ARRAY_F_CONTIGUOUS;
        }
        ret = (PyArrayObject *)\
            PyArray_NewFromDescr(subtype, descr,
                                 dims.len, dims.ptr,
                                 strides.ptr,
                                 offset + (char *)buffer.ptr,
                                 buffer.flags, NULL);
        if (ret == NULL) {
            descr = NULL;
            goto fail;
        }
        PyArray_UpdateFlags(ret, NPY_ARRAY_UPDATE_ALL);
        Py_INCREF(buffer.base);
        if (PyArray_SetBaseObject(ret, buffer.base) < 0) {
            Py_DECREF(ret);
            ret = NULL;
            goto fail;
        }
    }

    PyDimMem_FREE(dims.ptr);
    if (strides.ptr) {
        PyDimMem_FREE(strides.ptr);
    }
    return (PyObject *)ret;

 fail:
    Py_XDECREF(descr);
    if (dims.ptr) {
        PyDimMem_FREE(dims.ptr);
    }
    if (strides.ptr) {
        PyDimMem_FREE(strides.ptr);
    }
    return NULL;
}


static PyObject *
array_iter(PyArrayObject *arr)
{
    if (PyArray_NDIM(arr) == 0) {
        PyErr_SetString(PyExc_TypeError,
                        "iteration over a 0-d array");
        return NULL;
    }
    return PySeqIter_New((PyObject *)arr);
}

static PyObject *
array_alloc(PyTypeObject *type, Py_ssize_t NPY_UNUSED(nitems))
{
    PyObject *obj;
    /* nitems will always be 0 */
    obj = (PyObject *)_pya_malloc(NPY_SIZEOF_PYARRAYOBJECT);
    PyObject_Init(obj, type);
    return obj;
}


NPY_NO_EXPORT PyTypeObject PyArray_Type = {
#if defined(NPY_PY3K)
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                                          /* ob_size */
#endif
    "numpy.ndarray",                            /* tp_name */
    NPY_SIZEOF_PYARRAYOBJECT,                   /* tp_basicsize */
    0,                                          /* tp_itemsize */
    /* methods */
    (destructor)array_dealloc,                  /* tp_dealloc */
    (printfunc)NULL,                            /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
#if defined(NPY_PY3K)
    0,                                          /* tp_reserved */
#else
    0,                                          /* tp_compare */
#endif
    (reprfunc)array_repr,                       /* tp_repr */
    &array_as_number,                           /* tp_as_number */
    &array_as_sequence,                         /* tp_as_sequence */
    &array_as_mapping,                          /* tp_as_mapping */
    (hashfunc)0,                                /* tp_hash */
    (ternaryfunc)0,                             /* tp_call */
    (reprfunc)array_str,                        /* tp_str */
    (getattrofunc)0,                            /* tp_getattro */
    (setattrofunc)0,                            /* tp_setattro */
    &array_as_buffer,                           /* tp_as_buffer */
    (Py_TPFLAGS_DEFAULT
#if !defined(NPY_PY3K)
     | Py_TPFLAGS_CHECKTYPES
#endif
#if (PY_VERSION_HEX >= 0x02060000) && (PY_VERSION_HEX < 0x03000000)
     | Py_TPFLAGS_HAVE_NEWBUFFER
#endif
     | Py_TPFLAGS_BASETYPE),                  /* tp_flags */
    0,                                          /* tp_doc */

    (traverseproc)0,                            /* tp_traverse */
    (inquiry)0,                                 /* tp_clear */
    (richcmpfunc)array_richcompare,             /* tp_richcompare */
    offsetof(PyArrayObject_fields, weakreflist), /* tp_weaklistoffset */
    (getiterfunc)array_iter,                    /* tp_iter */
    (iternextfunc)0,                            /* tp_iternext */
    array_methods,                              /* tp_methods */
    0,                                          /* tp_members */
    array_getsetlist,                           /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    (initproc)0,                                /* tp_init */
    array_alloc,                                /* tp_alloc */
    (newfunc)array_new,                         /* tp_new */
    0,                                          /* tp_free */
    0,                                          /* tp_is_gc */
    0,                                          /* tp_bases */
    0,                                          /* tp_mro */
    0,                                          /* tp_cache */
    0,                                          /* tp_subclasses */
    0,                                          /* tp_weaklist */
    0,                                          /* tp_del */
#if PY_VERSION_HEX >= 0x02060000
    0,                                          /* tp_version_tag */
#endif
};

