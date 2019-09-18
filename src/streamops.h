/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef STREAMOPS_H
#define STREAMOPS_H

#define read8bval(file, num, value) \
   (fread((value), (num), 1, (file)) < 1 ? -1 : 0)
#define write8bval(file, num, value) \
   (fwrite((value), (num), 1, (file)) < 1 ? -1 : 0)
LOCAL_FN int read16bval(FILE* file, unsigned int num, void* value);
LOCAL_FN int write16bval(FILE* file, unsigned int num, const void* value);
LOCAL_FN int read24bval(FILE* file, unsigned int num, void* value);
LOCAL_FN int write24bval(FILE* file, unsigned int num, const void* value);
LOCAL_FN int read32bval(FILE* file, unsigned int num, void* value);
LOCAL_FN int write32bval(FILE* file, unsigned int num, const void* value);
LOCAL_FN int read64bval(FILE* file, unsigned int num, void* value);
LOCAL_FN int write64bval(FILE* file, unsigned int num, const void* value);
LOCAL_FN int read_double_field(FILE* file, double* val, unsigned int len);
LOCAL_FN int read_int_field(FILE* file, int* val, unsigned int len);
LOCAL_FN int read_string_field(FILE* file, char* val, unsigned int len);

#if WORDS_BIGENDIAN
# define LSB24	2
# define MSB24	0
#else
# define LSB24	0
# define MSB24	2
#endif

#endif /* STREAMOPS_H */

