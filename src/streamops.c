/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <mmsysio.h>

#include "streamops.h"
#include "common.h"

/**
 * read16bval() - read a given number of 16 bits integers from a file
 * @file: file from which the integers are read
 * @num: number of 16 bits integer to read
 * @value: buffer in which the reading is stored
 *
 * Return: 0 in case of success, -1 otherwise
 */
LOCAL_FN int read16bval(FILE* file, unsigned int num, void* value)
{
	if (fread(value, sizeof(uint16_t), num, file)==0)
		return -1;
#if WORDS_BIGENDIAN
	unsigned int i; 
	int16_t* buff16 = value;
	
	for (i=0; i<num; i++)
		buff16[i] = bswap_16(buff16[i]);
#endif
	return 0;
}


/**
 * write16bval() - write a given number of 16 bits integers in a file
 * @file: file in which the integers are written
 * @num: number of 16 bits integers to write
 * @value: buffer from which the integers are read
 *
 * Return: 0 in case of success, -1 otherwise
 */
LOCAL_FN int write16bval(FILE* file, unsigned int num, const void* value)
{
#if WORDS_BIGENDIAN
	unsigned int i; 
	const int16_t *ibuff16 = value;
	int16_t obuff16[num];
	
	for (i=0; i<num; i++)
		obuff16[i] = bswap_16(ibuff16[i]);

	value = obuff16;
#endif
	if (fwrite(value, sizeof(uint16_t), num, file)==0)
		return -1;
	return 0;
}


/**
 * read24bval() - read a given number of 24 bits integers from a file
 * @file: file from which the integers are read
 * @num: number of 24 bits integer to read
 * @value: buffer in which the reading is stored
 *
 * Return: 0 in case of success, -1 otherwise
 */
LOCAL_FN int read24bval(FILE* file, unsigned int num, void* value)
{
	if (fread(value, 3, num, file)==0)
		return -1;
#if WORDS_BIGENDIAN
	unsigned int i;
	int8_t* buff8 = value;
	char tmp;
	
	for (i=0; i<num*3; i+=3) {
		tmp = buff8[i];
		buff8[i] = buff8[i+2];
		buff8[i+2] = tmp;
	}
#endif
	return 0;
}


/**
 * write24bval() - write a given number of 24 bits integers in a file
 * @file: file in which the integers are written
 * @num: number of 24 bits integers to write
 * @value: buffer from which the integers are read
 *
 * Return: 0 in case of success, -1 otherwise
 */
LOCAL_FN int write24bval(FILE* file, unsigned int num, const void* value)
{
#if WORDS_BIGENDIAN
	unsigned int i;
	const int8_t *ibuff8 = value;
	int8_t obuff8[3*num];
	
	for (i=0; i<num*3; i+=3) {
		obuff8[i+2] = ibuff8[i];
		obuff8[i+1] = ibuff8[i+1];
		obuff8[i] = ibuff8[i+2];
	}

	value = obuff8;
#endif
	if (fwrite(value, 3, num, file)==0)
		return -1;
	return 0;
}


/**
 * read32bval() - read a given number of 32 bits integers from a file
 * @file: file from which the integers are read
 * @num: number of 32 bits integer to read
 * @value: buffer in which the reading is stored
 *
 * Return: 0 in case of success, -1 otherwise
 */
LOCAL_FN int read32bval(FILE* file, unsigned int num, void* value)
{
	if (fread(value, sizeof(uint32_t), num, file)==0)
		return -1;
#if WORDS_BIGENDIAN
	unsigned int i; 
	int32_t* buff32 = value;
	
	for (i=0; i<num; i++)
		buff32[i] = bswap_32(buff32[i]);
#endif
	return 0;
}


/**
 * write32bval() - write a given number of 32 bits integers in a file
 * @file: file in which the integers are written
 * @num: number of 32 bits integers to write
 * @value: buffer from which the integers are read
 *
 * Return: 0 in case of success, -1 otherwise
 */
LOCAL_FN int write32bval(FILE* file, unsigned int num, const void* value)
{
#if WORDS_BIGENDIAN
	unsigned int i; 
	const int32_t *ibuff32 = value;
	int32_t obuff32[num];
	
	for (i=0; i<num; i++)
		obuff32[i] = bswap_32(ibuff32[i]);

	value = obuff32;
#endif
	if (fwrite(value, sizeof(uint32_t), num, file)==0)
		return -1;
	return 0;
}


/**
 * read64bval() - read a given number of 64 bits integers from a file
 * @file: file from which the integers are read
 * @num: number of 64 bits integer to read
 * @value: buffer in which the reading is stored
 *
 * Return: 0 in case of success, -1 otherwise
 */
LOCAL_FN int read64bval(FILE* file, unsigned int num, void* value)
{
	if (fread(value, sizeof(uint64_t), num, file)==0)
		return -1;
#if WORDS_BIGENDIAN
	unsigned int i; 
	int64_t* buff64 = value;
	
	for (i=0; i<num; i++)
		buff64[i] = bswap_64(buff64[i]);
#endif
	return 0;
}


/**
 * write64bval() - write a given number of 64 bits integers in a file
 * @file: file in which the integers are written
 * @num: number of 64 bits integers to write
 * @value: buffer from which the integers are read
 *
 * Return: 0 in case of success, -1 otherwise
 */
LOCAL_FN int write64bval(FILE* file, unsigned int num, const void* value)
{
#if WORDS_BIGENDIAN
	unsigned int i; 
	const int64_t *ibuff64 = value;
	int64_t obuff64[num];
	
	for (i=0; i<num; i++)
		obuff64[i] = bswap_64(ibuff64[i]);

	value = obuff64;
#endif
	if (fwrite(value, sizeof(uint64_t), num, file)==0)
		return -1;
	return 0;
}


/**
 * read_int_field() - read a given number of character in a file.
 * @file: file in which the integers are read
 * @val: pointer fill with the value read
 * @nch: number of characters read from the file
 *
 * The value read corresponds to an integer.
 *
 * Return: 0 in case of success, -1 otherwise
 */
LOCAL_FN int read_int_field(FILE* file, int* val, unsigned int nch)
{
	char format[8];
	long pos = ftell(file);
	snprintf(format, sizeof(format), "%%%ui", nch);
	
	if ((fscanf(file, format, val) <= 0)
	    || fseek(file, pos+nch, SEEK_SET)) 
		return -1;
	
	return 0;
}


/**
 * read_int_field() - read a given number of character in a file.
 * @file: file in which the integers are read
 * @val: pointer fill with the value read
 * @nch: number of characters read from the file
 *
 * The value read corresponds to a string and does not contain trailing space.
 *
 * Return: 0 in case of success, -1 otherwise
 */
LOCAL_FN int read_string_field(FILE* file, char* val, unsigned int nch)
{
	int pos;

	val[nch] = '\0';
	if (fread(val, nch, 1, file) < 1)
		return -1;

	// Remove trailing spaces
	pos = strlen(val);
	while (pos && (val[pos-1]==' '))
		pos--;
	val[pos] = '\0';

	return 0;
}


