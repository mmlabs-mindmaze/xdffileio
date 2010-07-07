#ifndef BDF_H
#define BDF_H


struct xdf* xdf_alloc_bdffile(void);
int xdf_is_bdffile(const unsigned char* magickey);


#endif //BDF_H
