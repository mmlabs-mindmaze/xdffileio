#ifndef STREAMOPS_H
#define STREAMOPS_H

XDF_LOCAL int read16bval(FILE* file, unsigned int num, void* value);
XDF_LOCAL int write16bval(FILE* file, unsigned int num, const void* value);
XDF_LOCAL int read32bval(FILE* file, unsigned int num, void* value);
XDF_LOCAL int write32bval(FILE* file, unsigned int num, const void* value);
XDF_LOCAL int read64bval(FILE* file, unsigned int num, void* value);
XDF_LOCAL int write64bval(FILE* file, unsigned int num, const void* value);
XDF_LOCAL int read_double_field(FILE* file, double* val, unsigned int len);
XDF_LOCAL int read_int_field(FILE* file, int* val, unsigned int len);
XDF_LOCAL int read_string_field(FILE* file, char* val, unsigned int len);

#endif /* STREAMOPS_H */

