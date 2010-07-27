#ifndef XDFTYPES_H
#define XDFTYPES_H

#include <stdint.h>
#include "xdfio.h"

#ifndef INT24_MAX
#define INT24_MAX	8388607
#endif
#ifndef INT24_MIN
#define INT24_MIN	-8388608
#endif


union generic_data {
	double d;
	float f;
};

// Prototype of a type conversion preocedure
typedef void (*convproc)(unsigned int, void*, unsigned int, const void*, unsigned int);
typedef void (*scproc)(unsigned int, void*, const union generic_data);

// Parameters of a type conversion
struct convprm {
	unsigned int stride1, stride2, stride3;
	union generic_data scale;
	convproc cvfn1;
	scproc scfn2;
	convproc cvfn3;
};

void xdf_transconv_data(unsigned int ns, void* dst, void* src, const struct convprm* prm, void* tmpbuff);
int xdf_get_datasize(enum xdftype type);
int xdf_setup_transform(struct convprm* prm, 
		    unsigned int in_str, enum xdftype in_tp, const double in_mm[2], 
		    unsigned int out_str, enum xdftype out_tp, const double out_mm[2]);



#endif /* XDFTYPES_H */
