#ifndef XDFIO_H
#define XDFIO_H

#ifdef __cplusplus
extern "C" {
#endif


enum xdftype
{
	XDFINT8 = 0,
	XDFUINT8,
	XDFINT16,
	XDFUINT16,
	XDFINT24,
	XDFUINT24,
	XDFINT32,
	XDFUINT32,
	XDFFLOAT,
	XDFDOUBLE,
	XDFINT64,
	XDFUINT64,
	XDF_NUM_DATA_TYPES
};

enum xdffiletype
{
	XDF_ANY = 0,
	XDF_EDF,
	XDF_EDFP,
	XDF_BDF,
	XDF_GDF
};

enum xdffield
{
	XDF_FIELD_NONE = 0,
	XDF_FIELD_RECORD_DURATION,	/* double      */
	XDF_FIELD_NSAMPLE_PER_RECORD,	/* int         */
	XDF_FIELD_SUBJ_DESC,		/* const char* */
	XDF_FIELD_REC_DESC		/* const char* */
};

enum xdfchfield
{
	XDF_CHFIELD_NONE = 0,
	XDF_CHFIELD_ARRAY_INDEX,	/* int		*/
	XDF_CHFIELD_ARRAY_OFFSET,	/* int 		*/
	XDF_CHFIELD_ARRAY_TYPE,		/* enum xdftype */
	XDF_CHFIELD_STORED_TYPE,	/* enum xdftype */
	XDF_CHFIELD_LABEL,	        /* const char*  */
	XDF_CHFIELD_PHYSICAL_MIN,	/* double 	*/
	XDF_CHFIELD_PHYSICAL_MAX,	/* double 	*/
	XDF_CHFIELD_DIGITAL_MIN,	/* double 	*/
	XDF_CHFIELD_DIGITAL_MAX,	/* double 	*/
	XDF_CHFIELD_UNIT,		/* const char*  */
	XDF_CHFIELD_TRANSDUCTER,	/* const char*  */
	XDF_CHFIELD_PREFILTERING,	/* const char*  */
	XDF_CHFIELD_RESERVED,		/* const char*	*/
};


#define XDF_WRITE	0
#define XDF_READ	1

typedef struct xdffile* hxdf;
typedef struct xdf_channel* hchxdf;

hxdf xdf_open(const char* filename, int mode, enum xdffiletype type);
int xdf_close(hxdf xdf);

int xdf_set_info(hxdf xdf, enum xdffield field, ...);
int xdf_get_info(hxdf xdf, enum xdffield field, ...);
int xdf_copy_info(hxdf dst, hxdf src);

hchxdf xdf_get_channel(hxdf xdf, unsigned int index);
hchxdf xdf_add_channel(hxdf xdf);
int xdf_setconf_channel(hchxdf ch, enum xdfchfield field, ...);
int xdf_getconf_channel(hchxdf ch, enum xdfchfield field, ...);
int xdf_copy_channel(hchxdf dst, hchxdf src);

int xdf_define_arrays(hxdf xdf, unsigned int narrays, unsigned int* strides);
int xdf_prepare_transfer(hxdf xdf);

int xdf_write(hxdf xdf, unsigned int ns, ...);
int xdf_read(hxdf xdf, unsigned int ns, ...);

int xdf_get_error(hxdf xdf);
const char* xdf_get_string(void);

#ifdef __cplusplus
}
#endif


#endif //XDFIO_H
