/*
	Copyright (C) 2010  EPFL (Ecole Polytechnique Fédérale de Lausanne)
	Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef EBDF_H
#define EBDF_H


XDF_LOCAL struct xdf* xdf_alloc_bdffile(void);
XDF_LOCAL int xdf_is_bdffile(const unsigned char* magickey);

XDF_LOCAL struct xdf* xdf_alloc_edffile(void);
XDF_LOCAL int xdf_is_edffile(const unsigned char* magickey);


#endif //EBDF_H
