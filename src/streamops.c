#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "streamops.h"


XDF_LOCAL int read16bval(FILE* file, unsigned int num, void* value)
{
	if (fread(value, sizeof(uint16_t), num, file)==0)
		return -1;
	return 0;
}


XDF_LOCAL int write16bval(FILE* file, unsigned int num, const void* value)
{
	if (fwrite(value, sizeof(uint16_t), num, file)==0)
		return -1;
	return 0;
}


XDF_LOCAL int read32bval(FILE* file, unsigned int num, void* value)
{
	if (fread(value, sizeof(uint32_t), num, file)==0)
		return -1;
	return 0;
}


XDF_LOCAL int write32bval(FILE* file, unsigned int num, const void* value)
{
	if (fwrite(value, sizeof(uint32_t), num, file)==0)
		return -1;
	return 0;
}


XDF_LOCAL int read64bval(FILE* file, unsigned int num, void* value)
{
	if (fread(value, sizeof(uint64_t), num, file)==0)
		return -1;
	return 0;
}


XDF_LOCAL int write64bval(FILE* file, unsigned int num, const void* value)
{
	if (fwrite(value, sizeof(uint64_t), num, file)==0)
		return -1;
	return 0;
}

/* Parse the file (field of nch characters) and assign to the integer val.
 * Advance the file pointer of exactly nch byte.
 */
XDF_LOCAL int read_int_field(FILE* file, int* val, unsigned int nch)
{
	char format[8];
	long pos = ftell(file);
	sprintf(format, "%%%ui", nch);
	
	if ((fscanf(file, format, val) <= 0)
	    || fseek(file, pos+nch, SEEK_SET)) 
		return -1;
	
	return 0;
}


/* Parse the file (field of nch characters) and assign to the string val.
 * Advance the file pointer of exactly nch byte.
 * It also removes trailing space characters from the string
 */
XDF_LOCAL int read_string_field(FILE* file, char* val, unsigned int nch)
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


