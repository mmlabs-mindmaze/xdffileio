#ifndef XDFFILE_H
#define XDFFILE_H


#include <pthread.h>
#include <semaphore.h>

#include "xdfio.h"

#define TYPE_INT		0
#define TYPE_STRING		1
#define TYPE_DATATYPE		2
#define TYPE_DOUBLE		3

union optval {
	int i;
	const char* str;
	enum xdftype type;
	double d;
};

struct format_operations {
	int (*set_channel)(struct xdfch*, enum xdfchfield, union optval);
	int (*get_channel)(const struct xdfch*, enum xdfchfield, union
optval*);
	int (*copy_chconf)(struct xdfch*, const struct xdfch*);
	struct xdfch* (*alloc_channel)(void);
	void (*free_channel)(struct xdfch*);
	int (*set_conf)(struct xdf*, enum xdffield, union optval); 
	int (*get_conf)(const struct xdf*, enum xdffield, union optval*); 
	int (*copy_conf)(struct xdf*, const struct xdf*); 
	int (*write_header)(struct xdf*);
	int (*read_header)(struct xdf*);
	int (*close_file)(struct xdf*);
};

struct xdfch {
	unsigned int iarray, offset;
	enum xdftype inmemtype, infiletype;
	double physical_mm[2], digital_mm[2];
	struct xdfch* next;
	const struct format_operations* ops;
};

struct xdf {
	enum xdffiletype ftype;
	int fd;					
	unsigned int ready, mode;			
	long pointer;			
	double rec_duration;
	unsigned int ns_buff, ns_per_rec, sample_size;
	int nrecord;
	void *buff, *backbuff;		
	void *tmpbuff[2];
	int error;
	
	unsigned int numch;
	struct xdfch* channels;
	struct convertion_data* convdata;
	unsigned int nbatch;
	struct data_batch* batch;
	unsigned int narrays;
	unsigned int* array_stride;	
	const char** array_pos;	

	/* Data format specific behavior */
	const struct format_operations* ops;
	
	/* Background thread synchronization object */
	pthread_t thid;
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	sem_t sem;
	int order;
};

enum xdffiletype guess_file_type(const unsigned char* magickey);
struct xdf* alloc_xdffile(enum xdffiletype type);


#endif /* XDFFILE_H */
