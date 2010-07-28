#if HAVE_CONFIG_H
# include <config.h>
#endif


#include "xdftypes.h"
#include <stdint.h>
#include <string.h>

struct data_information {
	unsigned int size;
	unsigned int is_int:1;
	unsigned int is_signed:1;
};

union ui24 {
	int32_t i32;
	uint32_t u32;
	uint8_t p24[4];
};

#ifdef WORDS_BIGENDIAN

#  define copy_ui24_p8(src, pdst) 	\
	do { 					\
		pdst[0] = (src).p24[1];		\
		pdst[1] = (src).p24[2];		\
		pdst[2] = (src).p24[3];		\
	} while (0)

#else /* WORDS_BIGENDIAN */

#  define copy_ui24_p8(src, pdst) 	\
	do { 					\
		pdst[0] = (src).p24[0];		\
		pdst[1] = (src).p24[1];		\
		pdst[2] = (src).p24[2];		\
	} while (0)

#endif /* WORDS_BIGENDIAN */

static const struct data_information data_info[] = 
{
	[XDFFLOAT] = {.size = sizeof(float), .is_int = 0, .is_signed = 1},
	[XDFDOUBLE] = {.size = sizeof(double), .is_int = 0, .is_signed = 1},
	[XDFINT8] = {.size = sizeof(int8_t), .is_int = 1, .is_signed = 0},
	[XDFUINT8] = {.size = sizeof(uint8_t), .is_int = 1, .is_signed = 0},
	[XDFINT16] = {.size = sizeof(int16_t), .is_int = 1, .is_signed = 1},
	[XDFUINT16] = {.size = sizeof(uint16_t), .is_int = 1, .is_signed = 0},
	[XDFINT24] = {.size = 3, .is_int = 1, .is_signed = 1},
	[XDFUINT24] = {.size = 3, .is_int = 1, .is_signed = 0},
	[XDFINT32] = {.size = sizeof(int32_t), .is_int = 1, .is_signed = 1},
	[XDFUINT32] = {.size = sizeof(uint32_t), .is_int = 1, .is_signed = 0},
	[XDFINT64] = {.size = sizeof(int64_t), .is_int = 1, .is_signed = 1},
	[XDFUINT64] = {.size = sizeof(uint64_t), .is_int = 1, .is_signed = 0}
};



static void scale_data_d(unsigned int ns, void* data, const struct scaling_param* scaling)
{
	double* tdata = data;
	double sc = scaling->scale.d;
	double off = scaling->offset.d;
	while (ns--) {
		*tdata *= sc;
		*tdata += off;
		tdata++;
	}
}

static void scale_data_f(unsigned int ns, void* data, const struct scaling_param* scaling)
{
	float* tdata = data;
	float sc = scaling->scale.f;
	float off = scaling->offset.f;
	while (ns--) {
		*tdata *= sc;
		*tdata += off;
		tdata++;
	}
}

// Prototype of a generic type conversion function
#define DEFINE_CONV_FN(fnname, tsrc, tdst)				\
static void fnname(unsigned int ns, void* d, unsigned int std, const void* s, unsigned int sts)	\
{								\
	const tsrc* src = s;					\
	tdst* dst = d;						\
	while(ns--) {						\
		*dst = *src;					\
		dst = (tdst*)((char*)dst + std);			\
		src = (const tsrc*)((const char*)src + sts);	\
	}							\
}						

#define DEFINE_CONV_TO24_FN(fnname, otp, ctp)			\
static void fnname(unsigned int ns, void* d, unsigned int std, const void* s, unsigned int sts)	\
{								\
	const otp* src = s;					\
	int8_t* dst = d;					\
	union ui24 tmp;						\
	while (ns--) {						\
		tmp.ctp = *src;                                 \
		copy_ui24_p8(tmp, dst);                         \
		src = (const otp*)((const char*)src + sts); 	\
		dst += std;					\
	}							\
}

#define DEFINE_CONV_FROM24_FN(fnname, otp, intertp)		\
static void fnname(unsigned int ns, void* d, unsigned int std, const void* s, unsigned int sts)	\
{								\
	const intertp* src = s;                                \
	otp* dst = d;                                      	\
	while (ns--) {                                          \
		*dst = ((*src)<<8)>>8;                          \
		src = (const intertp*)((const char*)src + sts);\
		dst = (otp*)((char*)dst + std);			\
	}                                                       \
}                                                               
                                                                

// Declaration/definition of type conversion functions
DEFINE_CONV_FN(conv_i8_d, int8_t, double)
DEFINE_CONV_FN(conv_d_i8, double, int8_t)
DEFINE_CONV_FN(conv_i16_d, int16_t, double)
DEFINE_CONV_FN(conv_d_i16, double, int16_t)
DEFINE_CONV_FN(conv_i32_d, int32_t, double)
DEFINE_CONV_FN(conv_d_i32, double, int32_t)
DEFINE_CONV_FN(conv_u8_d, uint8_t, double)
DEFINE_CONV_FN(conv_d_u8, double, uint8_t)
DEFINE_CONV_FN(conv_u16_d, uint16_t, double)
DEFINE_CONV_FN(conv_d_u16, double, uint16_t)
DEFINE_CONV_FN(conv_u32_d, uint32_t, double)
DEFINE_CONV_FN(conv_d_u32, double, uint32_t)
DEFINE_CONV_FN(conv_i8_f, int8_t, float)
DEFINE_CONV_FN(conv_f_i8, float, int8_t)
DEFINE_CONV_FN(conv_i16_f, int16_t, float)
DEFINE_CONV_FN(conv_f_i16, float, int16_t)
DEFINE_CONV_FN(conv_i32_f, int32_t, float)
DEFINE_CONV_FN(conv_f_i32, float, int32_t)
DEFINE_CONV_FN(conv_u8_f, uint8_t, float)
DEFINE_CONV_FN(conv_f_u8, float, uint8_t)
DEFINE_CONV_FN(conv_u16_f, uint16_t, float)
DEFINE_CONV_FN(conv_f_u16, float, uint16_t)
DEFINE_CONV_FN(conv_u32_f, uint32_t, float)
DEFINE_CONV_FN(conv_f_u32, float, uint32_t)
DEFINE_CONV_FN(conv_f_d, float, double)
DEFINE_CONV_FN(conv_d_f, double, float)
DEFINE_CONV_FN(conv_i8_i64, int8_t, int64_t)
DEFINE_CONV_FN(conv_i64_i8, int64_t, int8_t)
DEFINE_CONV_FN(conv_i16_i64, int16_t, int64_t)
DEFINE_CONV_FN(conv_i64_i16, int64_t, int16_t)
DEFINE_CONV_FN(conv_i32_i64, int32_t, int64_t)
DEFINE_CONV_FN(conv_i64_i32, int64_t, int32_t)
DEFINE_CONV_FN(conv_u8_u64, uint8_t, uint64_t)
DEFINE_CONV_FN(conv_u64_u8, uint64_t, uint8_t)
DEFINE_CONV_FN(conv_u16_u64, uint16_t, uint64_t)
DEFINE_CONV_FN(conv_u64_u16, uint64_t, uint16_t)
DEFINE_CONV_FN(conv_u32_u64, uint32_t, uint64_t)
DEFINE_CONV_FN(conv_u64_u32, uint64_t, uint32_t)
DEFINE_CONV_FN(conv_f_i64, float, int64_t)
DEFINE_CONV_FN(conv_i64_f, int64_t, float)
DEFINE_CONV_FN(conv_d_i64, double, int64_t)
DEFINE_CONV_FN(conv_i64_d, int64_t, double)
DEFINE_CONV_FN(conv_f_u64, float, uint64_t)
DEFINE_CONV_FN(conv_u64_f, uint64_t, float)
DEFINE_CONV_FN(conv_d_u64, double, uint64_t)
DEFINE_CONV_FN(conv_u64_d, uint64_t, double)
DEFINE_CONV_FN(conv_ui8_ui8, int8_t, int8_t)
DEFINE_CONV_FN(conv_ui16_ui16, int16_t, int16_t)
DEFINE_CONV_FN(conv_ui32_ui32, int32_t, int32_t)
DEFINE_CONV_FN(conv_ui64_ui64, int64_t, int64_t)
DEFINE_CONV_FN(conv_i32_i16, int32_t, int16_t)
DEFINE_CONV_FN(conv_i16_i32, int16_t, int32_t)
DEFINE_CONV_FN(conv_u32_u16, uint32_t, uint16_t)
DEFINE_CONV_FN(conv_u16_u32, uint16_t, uint32_t)
DEFINE_CONV_FN(conv_f_f, float, float)
DEFINE_CONV_FN(conv_d_d, double, double)

DEFINE_CONV_TO24_FN(conv_ui64_ui24, int64_t, i32)
DEFINE_CONV_FROM24_FN(conv_i24_i64, int64_t, int32_t)
DEFINE_CONV_FROM24_FN(conv_u24_u64, uint64_t, uint32_t)
DEFINE_CONV_TO24_FN(conv_ui32_ui24, int32_t, i32)
DEFINE_CONV_FROM24_FN(conv_i24_i32, int32_t, int32_t)
DEFINE_CONV_FROM24_FN(conv_u24_u32, uint32_t, uint32_t)
DEFINE_CONV_TO24_FN(conv_f_i24, float, i32)
DEFINE_CONV_TO24_FN(conv_f_u24, float, u32)
DEFINE_CONV_FROM24_FN(conv_i24_f, float, int32_t)
DEFINE_CONV_FROM24_FN(conv_u24_f, float, uint32_t)
DEFINE_CONV_TO24_FN(conv_d_i24, double, i32)
DEFINE_CONV_TO24_FN(conv_d_u24, double, u32)
DEFINE_CONV_FROM24_FN(conv_i24_d, double, int32_t)
DEFINE_CONV_FROM24_FN(conv_u24_d, double, uint32_t)

static void conv_ui24_ui24(unsigned int ns, void* d, unsigned int std, const void *s, unsigned int sts)
{
	uint8_t *dstc = d;
	const uint8_t *srcc = s;

	while (ns--) {
		dstc[0] = srcc[0]; 
		dstc[1] = srcc[1]; 
		dstc[2] = srcc[2]; 
		dstc += std;
		srcc += sts;
	}
}

/* Table of data conversion procedure. Use it by calling convtable[srctype][dsttype]:
 e.g.: convtable[XDFUINT24][XDFDOUBLE] to get the function that convert
 unsigned int 24-bits into double*/
static const convproc convtable[XDF_NUM_DATA_TYPES][XDF_NUM_DATA_TYPES] = {
	[XDFUINT8] =  {[XDFUINT8] = conv_ui8_ui8, [XDFUINT64] = conv_u8_u64, [XDFFLOAT] = conv_u8_f, [XDFDOUBLE] = conv_u8_d},
	[XDFINT8] =   {[XDFINT8] = conv_ui8_ui8,  [XDFINT64] = conv_i8_i64,  [XDFFLOAT] = conv_i8_f, [XDFDOUBLE] = conv_i8_d},
	[XDFUINT16] = {[XDFUINT16] = conv_ui16_ui16, [XDFUINT32] = conv_u16_u32, [XDFUINT64] = conv_u16_u64, [XDFFLOAT] = conv_u16_f, [XDFDOUBLE] = conv_u16_d},
	[XDFINT16] =  {[XDFINT16] = conv_ui16_ui16, [XDFINT32] = conv_i16_i32, [XDFINT64] = conv_i16_i64, [XDFFLOAT] = conv_i16_f, [XDFDOUBLE] = conv_i16_d},
	[XDFUINT24] = {[XDFUINT24] = conv_ui24_ui24, [XDFUINT32] = conv_u24_u32, [XDFUINT64] = conv_u24_u64, [XDFFLOAT] = conv_u24_f, [XDFDOUBLE] = conv_u24_d},
	[XDFINT24] =  {[XDFINT24] = conv_ui24_ui24, [XDFINT32] = conv_i24_i32, [XDFINT64] = conv_i24_i64, [XDFFLOAT] = conv_i24_f, [XDFDOUBLE] = conv_i24_d},
	[XDFUINT32] = {[XDFUINT16] = conv_u32_u16, [XDFUINT24] = conv_ui32_ui24, [XDFUINT32] = conv_ui32_ui32, [XDFUINT64] = conv_u32_u64, [XDFFLOAT] = conv_u32_f, [XDFDOUBLE] = conv_u32_d},
	[XDFINT32] =  {[XDFINT16] = conv_i32_i16, [XDFINT24] = conv_ui32_ui24, [XDFINT32] = conv_ui32_ui32, [XDFINT64] = conv_i32_i64, [XDFFLOAT] = conv_i32_f, [XDFDOUBLE] = conv_i32_d},
	[XDFUINT64] = {[XDFUINT8] = conv_u64_u8, [XDFUINT16] = conv_u64_u16, 
	               [XDFUINT24] = conv_ui64_ui24, [XDFUINT32] = conv_u64_u32, 
		       [XDFUINT64] = conv_ui64_ui64,
                       [XDFFLOAT] = conv_u64_f, [XDFDOUBLE] = conv_u64_d},
	[XDFINT64] =  {[XDFINT8] = conv_i64_i8, [XDFINT16] = conv_i64_i16, 
	               [XDFINT24] = conv_ui64_ui24, [XDFINT32] = conv_i64_i32, 
		       [XDFINT64] = conv_ui64_ui64,
		       [XDFFLOAT] = conv_i64_f, [XDFDOUBLE] = conv_i64_d},
	[XDFFLOAT] =  {[XDFUINT8] = conv_f_u8, [XDFINT8] = conv_f_i8,
	               [XDFUINT16] = conv_f_u16, [XDFINT16] = conv_f_i16,
	               [XDFUINT24] = conv_f_u24, [XDFINT24] = conv_f_i24,
	               [XDFUINT32] = conv_f_u32, [XDFINT32] = conv_f_i32,
	               [XDFUINT64] = conv_f_u64, [XDFINT64] = conv_f_i64,
		       [XDFFLOAT] = conv_f_f, [XDFDOUBLE] = conv_f_d},
	[XDFDOUBLE] = {[XDFUINT8] = conv_d_u8, [XDFINT8] = conv_d_i8,
	               [XDFUINT16] = conv_d_u16, [XDFINT16] = conv_d_i16,
	               [XDFUINT24] = conv_d_u24, [XDFINT24] = conv_d_i24,
	               [XDFUINT32] = conv_d_u32, [XDFINT32] = conv_d_i32,
	               [XDFUINT64] = conv_d_u64, [XDFINT64] = conv_d_i64,
		       [XDFFLOAT] = conv_d_f, [XDFDOUBLE] = conv_d_d}
};


/* Extract data from packed channel (as in the GDF file) and convert the data in the file into the data useable by user */
void xdf_transconv_data(unsigned int ns, void* dst, void* src, const struct convprm* prm, void* tmpbuff)
{
	void* in = src;
	void* out = dst;

	if (prm->cvfn1) {
		if (prm->cvfn3)
			out = tmpbuff;
		prm->cvfn1(ns, out, prm->stride2, in, prm->stride1);
		in = out;
	}

	if (prm->scfn2)
		prm->scfn2(ns, in, &(prm->scaling));

	if (prm->cvfn3) {
		out = dst;
		prm->cvfn3(ns, out, prm->stride3, in, prm->stride2);
	}
}

int xdf_setup_transform(struct convprm* prm, 
		    unsigned int in_str, enum xdftype in_tp, const double* in_mm, 
		    unsigned int out_str, enum xdftype out_tp, const double* out_mm)
{
	int scaling = 1;
	enum xdftype ti;
	double sc, off;

	// Initialize convertion structure
	memset(prm, 0, sizeof(*prm));
	prm->stride1 = in_str;
	prm->stride3 = out_str;

	// Test for the need of scaling function
	if (!in_mm || !out_mm || !memcmp(in_mm, out_mm, sizeof(in_mm)))
		scaling = 0;

	// Determine the intermediate type
	ti =  (data_info[out_tp].is_int) ? in_tp : out_tp;
	if (scaling && data_info[ti].is_int)
		ti = XDFDOUBLE;
	if (!scaling && (!convtable[ti][out_tp] || !convtable[in_tp][ti]))
		ti = data_info[in_tp].is_signed ? XDFINT64 : XDFUINT64;
	prm->stride2 = data_info[ti].size;
	
	// Setup the convertion functions if needed
	if ((in_tp != ti) || (data_info[in_tp].size != in_str))
		prm->cvfn1 = convtable[in_tp][ti];
	if ((ti != out_tp) || (data_info[out_tp].size != out_str))
		prm->cvfn3 = convtable[ti][out_tp];
	
	// Setup scaling
	if (scaling) {
		sc = (out_mm[1] - out_mm[0]) /(in_mm[1] - in_mm[0]);
		off = out_mm[0] - sc * in_mm[0];

		if (ti == XDFDOUBLE) {
			prm->scaling.scale.d = sc;		
			prm->scaling.offset.d = off;
			prm->scfn2 = scale_data_d;
		} else if (ti == XDFFLOAT) {
			prm->scaling.scale.f = sc;
			prm->scaling.offset.f = off;
			prm->scfn2 = scale_data_f;
		}
	}

	return 0;
}

int xdf_get_datasize(enum xdftype type)
{
	return (type <= XDF_NUM_DATA_TYPES) ? (int)(data_info[type].size) : -1;
}
