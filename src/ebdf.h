#ifndef EBDF_H
#define EBDF_H


struct xdf* xdf_alloc_bdffile(void);
int xdf_is_bdffile(const unsigned char* magickey);

struct xdf* xdf_alloc_edffile(void);
int xdf_is_edffile(const unsigned char* magickey);


#endif //EBDF_H
