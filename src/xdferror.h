#ifndef XDFERROR_H
#define XDFERROR_H

#define	XDF_OK		0
#define XDF_EGENERIC	-1
#define XDF_EFILEIO	-2


int xdf_get_error(struct xdffile* xdf);
int set_xdf_error(struct xdffile* xdf, int error);


#endif //XDFERROR_H
