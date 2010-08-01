#ifndef EBDF_H
#define EBDF_H


XDF_LOCAL struct xdf* xdf_alloc_bdffile(void);
XDF_LOCAL int xdf_is_bdffile(const unsigned char* magickey);

XDF_LOCAL struct xdf* xdf_alloc_edffile(void);
XDF_LOCAL int xdf_is_edffile(const unsigned char* magickey);


#endif //EBDF_H
