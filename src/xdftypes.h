#ifndef XDFTYPES_H
#define XDFTYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "xdfio.h"

#ifndef INT24_MAX
#define INT24_MAX	8388607
#endif
#ifndef INT24_MIN
#define INT24_MIN	-8388608
#endif
#ifndef UINT24_MAX
#define UINT24_MAX	16777216
#endif

struct data_information {
	unsigned char size;
	bool is_int;
	bool is_signed;
	double lim[2];
};


union generic_data {
	double d;
	float f;
};

struct scaling_param {
	union generic_data scale;
	union generic_data offset;
};

// Prototype of a type conversion preocedure
typedef void (*convproc)(unsigned int, void* restrict, unsigned int, const void* restrict, unsigned int);
typedef void (*scproc)(unsigned int, void*, const struct scaling_param*);

// Parameters of a type conversion
struct convprm {
	unsigned int stride1, stride2, stride3;
	struct scaling_param scaling;
	convproc cvfn1;
	scproc scfn2;
	convproc cvfn3;
};

XDF_LOCAL const struct data_information* xdf_datinfo(enum xdftype type);
XDF_LOCAL void xdf_transconv_data(unsigned int ns, void* restrict dst, void* restrict src, const struct convprm* prm, void* restrict tmpbuff);
XDF_LOCAL int xdf_get_datasize(enum xdftype type);
XDF_LOCAL int xdf_setup_transform(struct convprm* prm, 
	    unsigned int in_str, enum xdftype in_tp, const double in_mm[2], 
	    unsigned int out_str, enum xdftype out_tp, const double out_mm[2]);

XDF_LOCAL enum xdftype get_closest_type(enum xdftype target,
					const bool *supported_type);


#endif /* XDFTYPES_H */
