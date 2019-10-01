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

import numpy
import os
import unittest
import sys

# additional imports to test for memory leaks
import gc

from xdf import XFile, Channel

TEST_DIR = os.path.dirname(os.path.abspath(__file__)) + '/../../tests'
TEST_FILES = {'edf': 'ref128-13-97-50-11-7-1.edf',
              'bdf': 'ref128-13-97-50-11-7-1.bdf',
              'gdf1': 'ref128-13-97-50-11-7-1.gdf1',
              'gdf2': 'ref128-13-97-50-11-7-1.gdf2'}

TEST_OUTFILES = {'edf': 'test.out.edf',
                 'bdf': 'test.out.bdf',
                 'gdf1': 'test.out.gdf1',
                 'gdf2': 'test.out.gdf2'}


def test_file(filetype, action):
    if action == 'read':
        return os.path.join(TEST_DIR, TEST_FILES[filetype])
    elif action == 'write':
        return os.path.join(TEST_DIR, TEST_OUTFILES[filetype])

def precision(filetype):
    if filetype == 'gdf':
        return 6
    else:
        return 3


class TestXFileClass(unittest.TestCase):

    def setUp(self):
        for filetype in TEST_FILES:
            try:
                os.remove(test_file(filetype, 'write'))
            except FileNotFoundError:
                pass

    def tearDown(self):
        for filetype in TEST_FILES:
            try:
                os.remove(test_file(filetype, 'write'))
            except FileNotFoundError:
                pass
        # gc.garbage is a list if uncollectable objects
        # test that there are none
        self.assertFalse(gc.garbage)

    def test_read_simple(self):
        'read smoke test'
        for filetype in TEST_FILES:
            f = XFile(test_file(filetype, 'read'), 'r', filetype)
            data = f.read()
            # only test that we have a 2D array of floats
            self.assertIs(type(data), numpy.ndarray)
            self.assertGreater(len(data), 1)
            self.assertGreater(len(data[0]), 1)
            self.assertIs(type(data[0][0]), numpy.float64)

    def test_read_filter(self):
        f = XFile(test_file('gdf2', 'read'), 'r')
        data = f.read(['EEG0'])
        # only test that we have a 2D array of floats
        self.assertIs(type(data), numpy.ndarray)
        self.assertIs(type(data[0][0]), numpy.float64)
        self.assertEqual(len(data.shape), 2)
        ns, nch = data.shape
        self.assertEqual(nch, 1)

    def test_read_any(self):
        '''ensure filetype is optional when reading
           read -> read from file
        '''
        f = XFile(test_file('gdf2', 'read'), 'r')
        self.assertEqual(f.filetype, 'gdf')

    def test_readonly_fields(self):
        'ensure read-only fields cannot be written'
        f = XFile(test_file('gdf2', 'write'), 'w', 'gdf2')
        with self.assertRaises(AttributeError):
            f.len = 42
        with self.assertRaises(AttributeError):
            f.filename = 'dummy'
        with self.assertRaises(AttributeError):
            f.filetype = 'dummy'

    def test_readwrite_fields(self):
        'ensure that we can write on writable fields'
        f = XFile(test_file('gdf2', 'write'), 'w', 'gdf2')
        f.channels.append(Channel('test_channel', -2., 2., 'dummy'))
        self.assertEqual(len(f.channels), 1)
        self.assertEqual(f.channels[0], {'name':'test_channel',
                                         'physical_min': -2.,
                                         'physical_max': 2.,
                                         'unit':'dummy'})

        f.fs = 42.
        self.assertEqual(f.fs, 42.)

        f.record_time = 42.
        self.assertEqual(f.record_time, 42.)

        f.subject_desc = "dummy subject desc"
        self.assertEqual(f.subject_desc, "dummy subject desc")

        f.session_desc = "dummy session desc"
        self.assertEqual(f.session_desc, "dummy session desc")

    def test_write_simple(self):
        'read/write smoke test'
        for filetype in ['edf', 'bdf', 'gdf2']:
            filename = test_file(filetype, 'write')
            test_array = numpy.random.rand(7, 1) # ns = 7, nch = 1

            f = XFile(filename, 'w', filetype)
            f.fs = 42
            f.channels.append(Channel('test_channel', -2., 2., 'dummy'))
            f.write(test_array)

            # open back for reading
            f = XFile(filename, 'r', filetype)

            # test metadatas
            # self.assertEqual(f.fs, 42)
            self.assertEqual(len(f.channels), 1)
            self.assertEqual(f.channels[0], {'name':'test_channel',
                                             'physical_min': -2.,
                                             'physical_max': 2.,
                                             'unit':'dummy'})

            # test data
            data = f.read()
            # test the first few data only: xdf formats do not support writing
            # less than a record. Record length is not configurable.
            self.assertGreater(len(data), len(test_array))
            self.assertEqual(len(data[0]), 1)
            for i in range(len(test_array)):
                # TODO: be less permissive
                # The rounding error depends on the closest stored data type
                # eg. edf only supported 16-bits data types, and that comes with
                # severe changes during rounding ...
                self.assertAlmostEqual(data[i][0], test_array[i][0],
                                       places=precision(filetype))

    def test_write_many_channels(self):
        'read/write more realistic test'
        filetype = 'gdf2'
        filename = test_file(filetype, 'write')
        ns = 5 * 1000
        nch = 23
        test_array = numpy.random.rand(ns, nch)

        f = XFile(filename, 'w', filetype)
        f.fs = 500
        for i in range(nch):
            ch_name = 'test_channel_{:d}'.format(i)
            f.channels.append(Channel(ch_name, -2., 2., 'dummy'))
        f.write(test_array)

        # open back for reading
        f = XFile(filename, 'r', filetype)

        # test metadatas
        # self.assertEqual(f.fs, 42)
        self.assertEqual(len(f.channels), nch)
        for i in range(nch):
            ch_name = 'test_channel_{:d}'.format(i)
            self.assertEqual(f.channels[i], {'name': ch_name,
                                             'physical_min': -2.,
                                             'physical_max': 2.,
                                             'unit':'dummy'})

        # test data
        data = f.read()
        # test the first few data only: xdf formats do not support writing
        # less than a record. Record length is not configurable.
        self.assertGreaterEqual(len(data), ns)
        self.assertEqual(len(data[0]), nch)
        for i in range(ns):
            for j in range(nch):
                self.assertAlmostEqual(data[i][j], test_array[i][j],
                                       places=precision(filetype))

    def test_write_many_channels_transposed(self):
        'read/write more realistic test with transposed array'
        filetype = 'gdf2'
        filename = test_file(filetype, 'write')
        ns = 5 * 1000
        nch = 23
        test_array = numpy.random.rand(nch, ns).T

        f = XFile(filename, 'w', filetype)
        f.fs = 500
        for i in range(nch):
            ch_name = 'test_channel_{:d}'.format(i)
            f.channels.append(Channel(ch_name, -2., 2., 'dummy'))
        f.write(test_array)

        # open back for reading
        f = XFile(filename, 'r', filetype)

        # test metadatas
        # self.assertEqual(f.fs, 42)
        self.assertEqual(len(f.channels), nch)
        for i in range(nch):
            ch_name = 'test_channel_{:d}'.format(i)
            self.assertEqual(f.channels[i], {'name': ch_name,
                                             'physical_min': -2.,
                                             'physical_max': 2.,
                                             'unit':'dummy'})

        # test data
        data = f.read()
        # test the first few data only: xdf formats do not support writing
        # less than a record. Record length is not configurable.
        self.assertGreaterEqual(len(data), ns)
        self.assertEqual(len(data[0]), nch)
        for i in range(ns):
            for j in range(nch):
                self.assertAlmostEqual(data[i][j], test_array[i][j],
                                       places=precision(filetype))


    def test_write_gdf1(self):
        'writing gdf1 files raise an error'
        # remove and merge into test_write_simple() once fixed
        filename = test_file('gdf1', 'write')
        with self.assertRaises(NotImplementedError):
            f = XFile(filename, 'w', 'gdf1')

    def test_with_keyword(self):
        'test that enter/exit functions work as expected'
        with XFile(test_file('gdf2', 'read'), 'r') as f:
            data = f.read(['EEG0'])
            self.assertIs(type(data), numpy.ndarray)
            self.assertIs(type(data[0][0]), numpy.float64)
            self.assertEqual(len(data.shape), 2)
            ns, nch = data.shape
            self.assertEqual(nch, 1)

    def test_read_channel_index(self):
        '''
        test that when filtering on channels, the returned channel order
        is the requested one.
        '''
        filename = test_file('gdf2', 'read')
        f = XFile(filename, 'r', 'gdf2')

        self.assertGreater(len(f.channels), 2)
        ch0 = f.channels[0]
        ch1 = f.channels[1]
        data_01 = f.read([ch0['name'], ch1['name']])
        data_10 = f.read([ch1['name'], ch0['name']])

        # test that data_01 and data_10 are the same data
        # with swapped channels

        # same dimensions
        self.assertEqual(len(data_01.shape), 2)
        self.assertEqual(len(data_10.shape), 2)
        self.assertEqual(data_01.shape, data_10.shape)

        # same data
        self.assertTrue((data_01[:,0] == data_10[:,1]).all())
        self.assertTrue((data_01[:,1] == data_10[:,0]).all())

    def test_read_channels_chunk(self):
        '''
        test that when filtering on a required laps of time,
        the returned chunk of time is the requested one.
        '''
        filename = test_file('gdf2', 'read')
        f = XFile(filename, 'r', 'gdf2')

        start = 16
        end = 624

        data = f.read(chunk = (start, end))
        fulldata = f.read() # assumption: the function read without
                            # argument is correct

        # test that each channel read contains end - start samples
        self.assertEqual(data.shape[0], end - start + 1)
        self.assertEqual(data.shape[1], len(f.channels))

        # test that the data read in the chunk correspond to the
        # required data
        for i in range(start, end + 1):
                for chan in range(len(f.channels)):
                        self.assertEqual(data[i - start][chan], fulldata[i][chan])

    def test_read_channels_index_chunk(self):
        '''
        test that when filtering on channels and a certain laps of time,
        the returned channel order and chunk of time are the requested one.
        '''
        filename = test_file('gdf2', 'read')
        f = XFile(filename, 'r', 'gdf2')

        self.assertGreater(len(f.channels), 2)
        ch0 = f.channels[0]
        ch1 = f.channels[1]

        start = 18
        end = 128

        data_01 = f.read([ch0['name'], ch1['name']], (start, end))
        data_10 = f.read([ch1['name'], ch0['name']], (start, end))
        fulldata_01 = f.read([ch0['name'], ch1['name']]) # assumption: the function read with
                                                         # channels argument is correct
        fulldata_10 = f.read([ch1['name'], ch0['name']]) # assumption: the function read with
                                                         # channels argument is correct

        # test that each channel read contains end - start samples
        self.assertEqual(data_01.shape[0], end - start + 1)
        self.assertEqual(data_10.shape[0], end - start + 1)
        self.assertEqual(data_01.shape[1], 2)
        self.assertEqual(data_10.shape[1], 2)

        # test that data_01 and data_10 are the same data
        # with swapped channels between the chunck of time

        # same dimensions
        self.assertEqual(len(data_01.shape), 2)
        self.assertEqual(len(data_10.shape), 2)
        self.assertEqual(data_01.shape, data_10.shape)

        # same data
        self.assertTrue((data_01[:,0] == data_10[:,1]).all())
        self.assertTrue((data_01[:,1] == data_10[:,0]).all())

        # test that the data read in the chunk correspond to the
        # required data
        for chan in range(2):
                for i in range(start, end + 1):
                        self.assertEqual(data_01[i - start][chan], fulldata_01[i][chan])
                        self.assertEqual(data_10[i - start][chan], fulldata_10[i][chan])

    def test_read_channels_invalid_chunk(self):
        '''
        test that when the chunk is invalid (the endding time is smaller
        than the starting time), the filtering is done on the required
        channels for the whole laps of time of the recording.
        '''
        filename = test_file('gdf2', 'read')
        f = XFile(filename, 'r', 'gdf2')

        start = 124
        end = 16

        with self.assertRaises(IndexError):
                f.read(chunk = (start, end))

    def test_read_channels_index_invalid_chunk(self):
        '''
        test that when the chunk is invalid (the endding time is smaller
        than the starting time), the filtering is done on the required
        channels for the whole laps of time of the recording.
        '''
        filename = test_file('gdf2', 'read')
        f = XFile(filename, 'r', 'gdf2')

        self.assertGreater(len(f.channels), 2)
        ch0 = f.channels[0]
        ch1 = f.channels[1]

        start = 36
        end = 18

        with self.assertRaises(IndexError):
                f.read([ch0['name'], ch1['name']], (start, end))
