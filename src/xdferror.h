#ifndef XDFERROR_H
#define XDFERROR_H

#include "xdffile.h"


int xdf_get_error(struct xdffile* xdf);
int set_xdf_error(struct xdffile* xdf, int error);


#endif //XDFERROR_H
