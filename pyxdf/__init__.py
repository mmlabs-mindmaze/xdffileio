# Copyright (C) 2019 MindMaze
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

'''
xdffileio python language bindings
'''

import numpy
from typing import List
from . _pyxdf import _XFile


class XFile(_XFile):
    'xdf File object wrapper'
    def __init__(self, filename: str, mode: str, type: str = "any"):
        ''' init XFile object

        Args:
            filename: name of the file to open
            mode: "read"/"r" or "write"/"w"
                  write can be flagged exclusive to prevent overwriting
                  like with fopen: "wx"
            type: (optional when reading) any of the following:
                  'edf', 'edfp', 'bdf', 'gdf1', 'gdf2'
                  'gdf' is an alias for 'gdf2'

        Eg:
        >>> f1 = XFile('/path/to/file1.gdf', 'read')
        >>> f2 = XFile('/path/to/file2.gdf', 'w', 'gdf')
        >>> f3 = XFile('/path/to/file3.gdf', 'wx', 'bdf')
        '''
        super().__init__(filename, mode, type)

    def read(self, channels: List[str] = None, chunk: tuple = None):
        '''Read samples from the open xdf file.

        Args:
            channels: (optional) filter on given channels. Read all channels
                      if none specified.
                      The channels will be returned in the requested order.
            chunk: (optional) filter on a given laps of time (represented by
                      two numbers of samples written in chunk). Read the
                      whole recording if none specified. If the start value
                      is negative or greater than the total number of samples,
                      or if the end value is negative or greater than the
                      total number of samples, or in case the end value is
                      smaller than the start value an exception is thrown.

        Return: a 2D numpy array of the read samples between the two samples
                indicated by chunk.

        Eg.
        >>> data = f.read(channels=['eeg:1', 'eeg:2'], chunk=(start, end))

        Raises:
            IOError: low-level I/O error occurred while reading
            KeyError: the requested channels do not exist
            KeyboardInterrupt: if the call was manually interrupted
            EnvironmentError: on stale file handle. This an occur for NFS
            PermissionError: on permission errors
        '''
        if chunk is None:
            return self._read(channels, (0, self.len - 1))

        start, end = chunk
        if (start < 0
                or end < start
                or end >= self.len):
            errmsg = 'chunk {} not within limits {}' \
                         .format(chunk, (0, self.len - 1))
            raise IndexError(errmsg)

        return self._read(channels, chunk)

    def write(self, data):
        ''' Write given numpy array into the xdf file

        Args:
            data: 2D numpy array of size NCH * NS containing numpy.float64

        Raises:
            IOError: low-level I/O error occurred while reading
            KeyboardInterrupt: if the call was manually interrupted
            EnvironmentError: on stale file handle. This an occur for NFS
            PermissionError: on permission errors
        '''
        # numpy.ascontiguousarray() will not copy if possible
        return self._write(numpy.ascontiguousarray(data))

    def __str__(self):
        return '{} file: {}'.format(self.filetype, self.filename)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        del self


class Channel(dict):
    ''' Helper class to generate channel dictionary structures
        The pre-defined fields are
            - name
            - physical_min
            - physical_max
            - unit
        The Channel dict does not allow adding or removing keys.
        However, it's a dictionary and you can choose to use it
        as such.
    '''
    def __init__(self, name: str, physical_min: float,
                 physical_max: float, unit: str):
        super().__init__()
        super().__setitem__('name', name)
        super().__setitem__('physical_min', physical_min)
        super().__setitem__('physical_max', physical_max)
        super().__setitem__('unit', unit)

    def __setitem__(self, key, value):
        if key in self:
            super().__setitem__(key, value)
        else:
            raise TypeError('cannot change object - object is immutable')

    def _immutable(self, *args, **kws):
        raise TypeError('cannot change object - object is immutable')

    __delitem__ = _immutable
    pop = _immutable
    popitem = _immutable
    clear = _immutable
    setdefault = _immutable

__all__ = ['Channel', 'XFile']
