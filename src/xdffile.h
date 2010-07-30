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
	int (*set_channel)(struct xdfch*, enum xdffield,
	                   union optval, int);
	int (*get_channel)(const struct xdfch*, enum xdffield,
	                   union optval*, int);
	int (*copy_chconf)(struct xdfch*, const struct xdfch*);
	struct xdfch* (*alloc_channel)(void);
	void (*free_channel)(struct xdfch*);
	int (*set_conf)(struct xdf*, enum xdffield, union optval, int); 
	int (*get_conf)(const struct xdf*, enum xdffield,
	                union optval*, int); 
	int (*copy_conf)(struct xdf*, const struct xdf*); 
	int (*write_header)(struct xdf*);
	int (*read_header)(struct xdf*);
	int (*complete_file)(struct xdf*);
	void (*free_file)(struct xdf*);
	enum xdffiletype type;
};

struct xdfch {
	int iarray, offset, digital_inmem;
	enum xdftype inmemtype, infiletype;
	double physical_mm[2], digital_mm[2];
	struct xdfch* next;
	struct xdf* owner;
};

struct xdf {
	enum xdffiletype ftype;
	int fd;					
	off_t hdr_offset;
	unsigned int ready, mode;			
	long pointer;			
	double rec_duration;
	unsigned int ns_buff, ns_per_rec, sample_size, filerec_size;
	int nrecord, nrecread;
	char *buff, *backbuff;		
	void *tmpbuff[2];
	int reportval;
	
	unsigned int numch;
	struct xdfch* channels;
	struct convertion_data* convdata;
	unsigned int nbatch;
	struct data_batch* batch;
	unsigned int narrays;
	unsigned int* array_stride;	

	struct xdfch* defaultch;
	/* Data format specific behavior */
	const struct format_operations* ops;
	
	/* Background thread synchronization object */
	pthread_t thid;
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	sem_t sem;
	int order;
};

enum xdffiletype xdf_guess_filetype(const unsigned char* magickey);
struct xdf* xdf_alloc_file(enum xdffiletype type);
struct xdfch* xdf_alloc_channel(struct xdf* owner);
int xdf_set_error(int error);


#endif /* XDFFILE_H */
