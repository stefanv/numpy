/*
 * This file includes all the .cpp files needed for a complete multiarray module.
 * This is used in the case where separate compilation is not enabled
 *
 * Note that the order of the includs matters
 */

#include "common.cpp"

#include "scalartypes.cpp"
#include "scalarapi.cpp"

#include "datetime.cpp"
#include "datetime_strings.cpp"
#include "datetime_busday.cpp"
#include "datetime_busdaycal.cpp"
#include "arraytypes.cpp"

#include "hashdescr.cpp"
#include "numpyos.cpp"

#include "descriptor.cpp"
#include "flagsobject.cpp"
#include "ctors.cpp"
#include "iterators.cpp"
#include "mapping.cpp"
#include "number.cpp"
#include "getset.cpp"
#include "sequence.cpp"
#include "methods.cpp"
#include "convert_datatype.cpp"
#include "convert.cpp"
#include "shape.cpp"
#include "item_selection.cpp"
#include "calculation.cpp"
#include "usertypes.cpp"
#include "refcount.cpp"
#include "conversion_utils.cpp"
#include "buffer.cpp"

#include "nditer_constr.cpp"
#include "nditer_api.cpp"
#include "nditer_templ.cpp"
#include "nditer_pywrap.cpp"
#include "lowlevel_strided_loops.cpp"
#include "dtype_transfer.cpp"
#include "einsum.cpp"
#include "array_assign.cpp"
#include "array_assign_scalar.cpp"
#include "array_assign_array.cpp"
#include "reduction.cpp"
#include "na_mask.cpp"
#include "na_object.cpp"
#include "boolean_ops.cpp"

#ifndef Py_UNICODE_WIDE
#include "ucsnarrow.cpp"
#endif

#include "arrayobject.cpp"

#include "numpymemoryview.cpp"

#include "multiarraymodule.cpp"
