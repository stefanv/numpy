:Title: A proposal for implementing enumerated types in NumPy
:Author: Bryan Van de Ven
:Contact: bryanv@continuum.io
:Date: 2012-03-08

*****************************************************
A proposal for implementing enumerated types in NumPy
*****************************************************

.. contents:: Table of Contents

Executive summary
=================
NumPy provides some functionality for storing fixed-length strings, but does not have any capability to 
treat textual names directly as values of an enumerated type. 
Desire has been expressed for enumerated types in NumPy, both for a traditional enum whose values are 
defined once and fixed, and for an enumerated type that can grow dynamically, as it encounters new names. 
This NEP describes two additional type kinds, a traditional (or 'closed') enum, and a dynamic 
(or 'open') enum. 


Examples of use
===============

*Question: should enum types allow the user to specify some or all of the numerical values, as is possible
in C? Or should enum types only expose the names as opaque things?*

*Question: what is the exact specification for permissable names?*


Construction
------------

Building closed enums by hand::

  # automatically generate numerical values, starting from zero, uses smallest possible storage size
  t = np.dtype('enum[A, B, C, D, E]')

  # explicitly provide all the numerical values
  t = np.dtype('enum[A:0, B:12, C:3, D:4, E:128]')

  # explicitly provide some of the numerical values, generate B=11, C=12, etc.
  t = np.dtype('enum[A:10, B, C, D, E:128]')

  # explicitly specify the storage size
  t = np.dtype('enum:uint16[A, B, C, D, E]')

Building closed enums using external sources::

  n = np.array(['A', 'B', 'C', 'D', 'E')
  v = np.array([10, 20, 3, 30, 128])

  # automatically generate numerical values, starting from zero
  t = np.dtype('enum', map=n)

  # explicitly provide all the numerical values
  t = np.dtype('enum', map=(n,v))

  # explicitly specify the storage size
  t = np.dtype('enum:uint32', map=n)

Building open enums::

  # automatically generate numerical values, starting from zero, defaults to largest storage size
  t = np.dtype('enum')

  # explicitly specify the storage size
  t = np.dtype('enum:uint32')

  # adapt an existing closed enum
  tc = np.dtype('enum[A:0, B:12, C:3, D:4, E:128]')
  t = np.dtype(tc)

*Question: the last example would always adapt closed enums to open, and leave no means to simply copy
a closed enum. Is there a different, more explicit way to spell these cases?*

Assignemnt
----------

Closed Enums::

  t = np.dtype('enum[A, B, C, D, E]')

  a = np.array(['A', 'A', 'E', 'D'], dtype=tclose)
  
  a[0] = 'B'
  
  a[0] = 'F' # ValueError!

Open Enums::

  t = np.dtype('enum')

  a = np.array(['A', 'A', 'E', 'D'], dtype=tclose)
  
  a[0] = 'B'
  
  a[0] = 'F' # OK!

Printing
--------

*Are there any special considerations here? 
What to do in the case of very long names?*

Implementation Details
======================

Mapping
-------

To implement the mapping between values and names, we will use KHash_, which is a small, header-only, 
efficient hashmap library in C. 
We will maintain both a map from names to values, as well as the inverse map from 
values to names. 

The name-to-value mapping will be stored as a Capsule in the dtype metadata dict under the key '__n2v__'. 
The value-to-name mapping will be stored as a Capsule in the dtype metadata dict under the key '__v2n__'. 
In addition to convenience, this scheme will maintain the immutability of dtypes in the case of open enums.

*Example code storing and retrieving a KHash in a Python Capule goes here*

Storage Size
------------
The element storage size will also be stored in the dtype metadata dict, under the key '__sz__'.


Serialization
-------------

*Here some input is needed. Writing out enums presents some questions centered around what to do with 
the name-value mapping in the case of ASCII formats.
Should NumPy refuse to save an array containing an enum field in an ASCII format?
Otherwise, should NumPy generate some sort of sidecar file?
If not, should the mapping be exportable somehow from the dtype as an array of names and values so 
that users can write their own sidecar files? 
How should these options be spelled in NumPy?* 

.. _KHash: http://attractivechaos.awardspace.com/khash.h.html

.. [1] http://docs.python.org/c-api/capsule.html#capsules

.. Local Variables:
.. mode: rst
.. coding: utf-8
.. fill-column: 72
.. End: