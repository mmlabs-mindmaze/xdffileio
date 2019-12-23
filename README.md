
Presentation
============

This library provides a unified interface to read/write EEG file format
in realtime. It has been designed to provide a consistent and common
interface to all supported file formats while minimizing the CPU cost on the
main loop. It thus performs all the expensive operation (scaling, data
convertion and file operation) in a separated thread. 

The library does not support non-continous recording neither channels
sampled at different sampling rate.

It currently supports the following file formats: EDF, BDF, GDF 1.X, GDF 2.X


Supported platforms
===================

Any POSIX platform (and windows) using a byte size of 8 bits (i.e. all
modern CPU) should be able to compile and run this library. However, it has
been only tested on the following platform: GNU/Linux x86, GNU/Linux x86-64,
Windows x86.


Dependencies
============

It depends on [mmlib](https://github.com/mmlabs-mindmaze/mmlib).

The Python package can be built if the python development files are installed


Compilation
===========
xdffileio supports meson and autotools build systems:

``` bash
# meson
meson build --prefix=<install-dir>
cd build
ninja
ninja test # optional
ninja install
```

``` bash
# autotools
mkdir build && cd build
../autogen.sh
../configure --prefix=<install-dir>
make
make check # optional
make install
```


License
=======

xdffileio is licensed in LGPL-3 or any later version
