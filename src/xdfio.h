#ifndef XDFIO_H
#define XDFIO_H

#include <sys/types.h>
#include <unistd.h>

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

#define XDF_F_FIRST	1
#define XDF_CF_FIRST	10001

enum xdffield
{
	XDF_NOF = 0,

	/* File configuration field */
	XDF_F_REC_DURATION = XDF_F_FIRST,/* double      */
	XDF_F_REC_NSAMPLE,		/* int         */
	XDF_F_SAMPLING_FREQ,		/* int         */
	XDF_F_NCHANNEL,			/* int         */
	XDF_F_SUBJ_DESC,		/* const char* */
	XDF_F_SESS_DESC,		/* const char* */

	/* Channel configuration fields */
	XDF_CF_ARRINDEX = XDF_CF_FIRST,/* int		*/
	XDF_CF_ARROFFSET,	/* int 		*/
	XDF_CF_ARRDIGITAL,	/* int		*/
	XDF_CF_ARRTYPE,		/* enum xdftype */
	XDF_CF_STOTYPE,		/* enum xdftype */
	XDF_CF_LABEL,	        /* const char*  */
	XDF_CF_PMIN,		/* double 	*/
	XDF_CF_PMAX,		/* double 	*/
	XDF_CF_DMIN,		/* double 	*/
	XDF_CF_DMAX,		/* double 	*/
	XDF_CF_UNIT,		/* const char*  */
	XDF_CF_TRANSDUCTER,	/* const char*  */
	XDF_CF_PREFILTERING,	/* const char*  */
	XDF_CF_RESERVED,	/* const char*	*/
};


#define XDF_WRITE	0
#define XDF_READ	1

struct xdf;
struct xdfch;

struct xdf* xdf_open(const char* filename, int mode, enum xdffiletype type);
int xdf_close(struct xdf* xdf);

int xdf_set_conf(struct xdf* xdf, enum xdffield field, ...);
int xdf_get_conf(const struct xdf* xdf, enum xdffield field, ...);
int xdf_copy_conf(struct xdf* dst, const struct xdf* src);

struct xdfch* xdf_get_channel(const struct xdf* xdf, unsigned int index);
struct xdfch* xdf_add_channel(struct xdf* xdf, const char* label);
int xdf_set_chconf(struct xdfch* ch, enum xdffield field, ...);
int xdf_get_chconf(const struct xdfch* ch, enum xdffield field, ...);
int xdf_copy_chconf(struct xdfch* dst, const struct xdfch* src);

int xdf_define_arrays(struct xdf* xdf, unsigned int narrays, unsigned int* strides);
int xdf_prepare_transfer(struct xdf* xdf);

ssize_t xdf_write(struct xdf* xdf, size_t ns, ...);
ssize_t xdf_read(struct xdf* xdf, size_t ns, ...);
off_t xdf_seek(struct xdf* xdf, off_t offset, int whence);


const char* xdf_get_string(void);

#ifdef __cplusplus
}
#endif


#endif //XDFIO_H
