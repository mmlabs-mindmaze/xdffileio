#ifndef XDFERROR_H
#define XDFERROR_H

#include "xdffile.h"


int xdf_get_error(const struct xdf* xdf);
int set_xdf_error(struct xdf* xdf, int error);


#endif //XDFERROR_H
