/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Copyright (C) 2013  Nicolas Bourdaud

    Authors:
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
#ifndef XDFIO_H
#define XDFIO_H

#include <sys/types.h>

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
	XDF_GDF1,
	XDF_GDF2,
	XDF_NUM_FILE_TYPES
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
	XDF_F_FILEFMT,			/* int		*/
	XDF_F_NEVTTYPE,			/* int         */
	XDF_F_NEVENT,			/* int         */
	XDF_F_NREC,			/* int         */

	/* Format specific file fields */
	XDF_F_SUBJ_DESC = 5000,		/* const char* */
	XDF_F_SESS_DESC,		/* const char* */
	XDF_F_RECTIME,			/* double      */
	XDF_F_ADDICTION,		/* unsigned int	*/
	XDF_F_BIRTHDAY,			/* double	*/
	XDF_F_HEIGHT,			/* double	*/
	XDF_F_WEIGHT,			/* double	*/
	XDF_F_GENDER,			/* unsigned int */
	XDF_F_HANDNESS,			/* unsigned int */
	XDF_F_VISUAL_IMP,		/* unsigned int */
	XDF_F_HEART_IMP,		/* unsigned int */
	XDF_F_LOCATION,			/* double[3]	*/
	XDF_F_ICD_CLASS,		/* char[6]	*/
	XDF_F_HEADSIZE,			/* double[3]	*/
	XDF_F_REF_POS,			/* double[3]	*/
	XDF_F_GND_POS,			/* double[3]	*/
		

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
	
	/* Format specific channel fields */
	XDF_CF_UNIT=20000,	/* const char*  */
	XDF_CF_TRANSDUCTER,	/* const char*  */
	XDF_CF_PREFILTERING,	/* const char*  */
	XDF_CF_RESERVED,	/* const char*	*/
	XDF_CF_ELECPOS,		/* double[3]	*/
	XDF_CF_IMPEDANCE	/* double	*/
};


#define XDF_WRITE	0
#define XDF_READ	1
#define XDF_CLOSEFD	0x10

struct xdf;
struct xdfch;

struct xdf* xdf_open(const char* filename, int mode,
   		enum xdffiletype type);
struct xdf* xdf_fdopen(int fd, int mode, enum xdffiletype type);
int xdf_close(struct xdf* xdf);

int xdf_set_conf(struct xdf* xdf, enum xdffield field, ...);
int xdf_get_conf(const struct xdf* xdf, enum xdffield field, ...);
int xdf_copy_conf(struct xdf* dst, const struct xdf* src);

int xdf_add_evttype(struct xdf* xdf, int code, const char* desc);
int xdf_get_evttype(struct xdf* xdf, unsigned int evttype,
               int *code, const char** desc);
int xdf_add_event(struct xdf* xdf, int evttype, double onset,
             double duration);
int xdf_get_event(struct xdf* xdf, unsigned int index, 
            unsigned int *evttype, double* start, double* dur);

struct xdfch* xdf_get_channel(const struct xdf* xdf,
   			unsigned int index);
struct xdfch* xdf_add_channel(struct xdf* xdf, const char* label);
int xdf_set_chconf(struct xdfch* ch, enum xdffield field, ...);
int xdf_get_chconf(const struct xdfch* ch, enum xdffield field,...);
int xdf_copy_chconf(struct xdfch* dst, const struct xdfch* src);

int xdf_define_arrays(struct xdf* xdf, unsigned int narrays,
   		const size_t* strides);
int xdf_prepare_transfer(struct xdf* xdf);

ssize_t xdf_write(struct xdf* xdf, size_t ns, ...);
ssize_t xdf_read(struct xdf* xdf, size_t ns, ...);
ssize_t xdf_writev(struct xdf* xdf, size_t ns, void** vbuff);
ssize_t xdf_readv(struct xdf* xdf, size_t ns, void** vbuff);
off_t xdf_seek(struct xdf* xdf, off_t offset, int whence);

int xdf_closest_type(const struct xdf* xdf, enum xdftype type);
const char* xdf_get_string(void);

#ifdef __cplusplus
}
#endif


#endif //XDFIO_H
