#ifndef XDFFORMATOPS_H
#define XDFFORMATOPS_H

#include <stdarg.h>
#include "xdfio.h"

struct format_operations {
	int (*set_channel)(struct xdf_channel*, enum xdfchfield, ...);
	int (*get_channel)(const struct xdf_channel*, enum xdfchfield, ...);
	int (*copy_channel)(struct xdf_channel*, const struct xdf_channel*);
	struct xdf_channel* (*alloc_channel)(void);
	void (*free_channel)(struct xdf_channel*);
	int (*set_info)(struct xdffile*, enum xdffield, ...); 
	int (*get_info)(const struct xdffile*, enum xdffield, ...); 
	int (*copy_info)(struct xdffile*, const struct xdffile*); 
	int (*write_header)(struct xdffile*);
	int (*read_header)(struct xdffile*);
	int (*close_file)(struct xdffile*);
};

enum xdffiletype guess_file_type(const unsigned char* magickey);
struct xdffile* alloc_xdffile(enum xdffiletype type);

#endif // XDFFORMATOPS_H
