Welcome to xdffileio's documentation!
=====================================

This library provides a unified interface to read/write EEG file format
in realtime. It has been designed to provide a consistent and common
interface to all supported file formats while minimizing the CPU cost on the
main loop. It thus performs all the expensive operation (scaling, data
convertion and file operation) in a separated thread. 

The library does not support non-continous recording neither channels
sampled at different sampling rate.

It currently supports the following file formats: EDF, BDF, GDF 1.X, GDF 2.X

.. toctree::
   :caption: API module list
   :titlesonly:
   :maxdepth: 2

   xdfio.rst


Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`

