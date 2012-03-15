:Title: A proposal for implementing factored dtypes in NumPy
:Author: Bryan Van de Ven
:Contact: bryanv@continuum.io
:Copyright: Copyright 2011 by Continuum Analytics, Inc
:License: CC By-SA 3.0 (http://creativecommons.org/licenses/by-sa/3.0/)
:Date: 2012-03-15

****************************************************
A proposal for implementing factored dtypes in NumPy
****************************************************

.. contents:: Table of Contents

Executive summary
=================
NumPy provides some functionality for storing fixed-length strings, but does not have any capability to treat textual names directly as values of an enumerated type or as a factor as in R. 
Desire has been expressed for this functionality in NumPy, both for a traditional enumerated type whose values are defined once and fixed, and for an enumerated type that can grow dynamically, as it encounters new names (for instance while ingesting a large text file). 
This NEP describes a mechanism for creating enumerated values and attaching them to existing integral types. 

Use Cases
=========
An enumerated type or factor in numpy must accomodate several use cases:

* Ingest arrays from text files where the list of factors is known and defined in advance
* Ingest arrays from text files where the list of factors is not known, or not defined in advance
* Adapt existing integral arrays or integral fields of arrays to conform to a factored dtype


Examples of use
===============
Construction
------------
We propse to add a single function, numpy.factor, that can create factored dtypes from existing integral dtypes, and adapt arrays to use factored dtypes.  

  # create a factored dtype with three levels, by default [0,1,2]
  fdt = factor(uint, ['a','b','c'])

  # create a factored dtype with three levels that correspond to user-supplied values, [10, 20, 30]
  fdt = factor(uint, {'a':10, 'b':20, 'c':30})

  # create a factored dtype with three levels and an explicit NA level corresponding to a 
  # user-supplied value
  fdt = factor(uint, ['a', 'b', 'c'])

  # create a factored dtype with three levels and an explicit NA level corresponding to any 
  # unknown value
  fdt = factor(uint, ['a', 'b', 'c'], na=True)

  # create a factored dtype with three levels, that can automatically accommodate new levels
  fdt = factor(uint, ['a', 'b', 'c'], open=True)

  # create a factored dtype with ordered levels
  fdt = factor(uint, ['Winter', 'Spring', 'Summer', 'Fall'], ordered=True)

Factored dtypes may be used in structured array dtypes for file reading:

  fdt = factor(uint, ['Winter', 'Spring', 'Summer', 'Fall'], ordered=True)
  dt = dtype([('season', fdt), ('cost', float)])
  a = fromfile(fname, dtype=dt)

You can adapt an existing array to an array with factored dtype:

  a = array([1, 1, 0, 2, 0, 1, 2])
  fa = factor(a, ['a', 'b', 'c'])

All of the same parameters apply when adapting arrays with factor.
Unless na=True, the array to be adapted must have values that correspond to known levels:

  a = array([4])
  fa = factor(a, ['a', 'b', 'c'])  # ValueError

With na=True, the array to be adapted may contain values that do not correspond to known levels; these are assigned to the NA level. 

  a = array([4])
  fa = factor(a, ['a', 'b', 'c'])  # OK, fa[0] == '[NA]'

Factored arrays can be "refactored":

  fa = factor(fa, ['a', 'b', 'c', 'd'])

Factored arrays can also be "unfactored":

  a = factor(fa, None)

Assignment
----------
Arrays and fields of arrays corresponding to factored dtypes only accept string inputs.
If the factored dtype does not have open=True, or na=True, then unknown levels raise a ValueError:

  fa = factor(array([1, 2, 0]), ['a', 'b', 'c'])
  fa[0] = 'a' # OK
  fa[0] = 'd' # ValueError
  fa[0] = 2   # ValueError

With open=True, unknown levels will be accepted, and added to the collection of factors:

  fa = factor(array([1, 2, 0]), ['a', 'b', 'c'], open=True)
  fa[0] = 'a' # OK
  fa[0] = 'd' # OK, f[0] == 'd'

With na=True, unknown levels will be accepted, and added to the NA level

  fa = factor(array([1, 2, 0]), ['a', 'b', 'c'], na=True)
  fa[0] = 'a' # OK
  fa[0] = 'd' # OK, f[0] == '[NA]'

A factored dtype may not have both open=True, and na=True. 


Printing
--------

  >>> a = array([1, 1, 0, 2, 0, 1, 2])
  >>> fa = factor(a, ['a', 'b', 'c'])
  >>> fa
  Levels(3): [a,b,c]
  array([b, b, a, c, a, b, c])

  >>> fa = factor(a, ['a', 'b'], na=True)
  >>> fa
  Levels(2): [a,b]
  array([b, b, a, NA, a, b, NA])

Serialization
-------------
I see two possibilities:
 
* write out the factor level names explicitly. This could be problematic width fixed with fields. Also, if na=True, information could be lost.
* write out the values, and provide a means to serialize the factored dtype, if the user desires. 

I am inclined to prefer the second possibility. 


Implementation Details
======================
Mapping
-------

To implement the mapping between values and names, we will use KHash_, which is a small, header-only, efficient hashmap library in C. 
We will maintain both a map from names to values, as well as the inverse map from values to names. 

The level-to-value mapping will be stored as a CObject in the dtype metadata dict under the key '__factor_n2v__'. 
The value-to-level mapping will be stored as a CObject in the dtype metadata dict under the key '__factor_v2n__'. 

*Example code storing and retrieving a KHash in a CObject goes here*

For closed factored dtypes, a khash with the smallest element size will be chosen, based on the number of levels supplied. 
(E.g., for a closed factored dtype with ten levels, khashes that map bytes to and from strings will be used.)
For open factored dtypes, khashes will always map C integers to and from strings. 

Serialization
-------------


.. _KHash: http://attractivechaos.awardspace.com/khash.h.html

.. Local Variables:
.. mode: rst
.. coding: utf-8
.. fill-column: 72
.. End: