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

import unittest

from xdf import Channel


REF_DICT = {'name':'name', 'physical_min': 0.,
            'physical_max': 1., 'unit':'unit'}

class TestChannelClass(unittest.TestCase):

    def test_simple(self):
        'smoke test'
        c = Channel('name', 0., 1., 'unit')
        self.assertEqual(c, REF_DICT)

    def test_constructor(self):
        'test with named args'
        c = Channel(name='name', physical_min=0.,
                    physical_max=1., unit='unit')
        self.assertEqual(c, REF_DICT)

    def test_update_key(self):
        'we can change the defined keys'
        c = Channel('name', 0., 1., 'unit')
        c['name'] = 'xxx'
        self.assertEqual(c['name'], 'xxx')

    def test_add_new_key(self):
        '''we can't add new keys'''
        c = Channel('name', 0., 1., 'unit')
        with self.assertRaises(TypeError):
            c['new_key'] = 0

    def test_remove_key(self):
        '''we can't remove keys'''
        c = Channel('name', 0., 1., 'unit')
        with self.assertRaises(TypeError):
            del c['name']
