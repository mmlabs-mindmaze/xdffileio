#ifndef XDFFILE_H
#define XDFFILE_H


#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include "xdfchannels.h"
#include "xdfio.h"

struct xdffile {
	enum xdffiletype ftype;
	int fd;					
	unsigned int ready, mode;			
	long pointer;			
	unsigned int ns_buff, ns_per_rec, sample_size;
	unsigned int num_array_input;
	int nrecord;
	void *buff, *backbuff;		
	void *tmpbuff[2];
	int error;
	
	unsigned int numch;
	struct xdf_channel* channels;
	struct convertion_data* convdata;
	unsigned int nbatch;
	struct data_batch* batch;
	unsigned int narrays;
	unsigned int* array_stride;	
	const char** array_pos;	

	/* Data format specific behavior */
	int (*set_channel_proc)(struct xdf_channel*, enum xdfchfield, ...);
	int (*get_channel_proc)(const struct xdf_channel*, enum xdfchfield, ...);
	int (*copy_channel_proc)(struct xdf_channel*, const struct xdf_channel*);
	struct xdf_channel* (*alloc_channel_proc)(void);
	int (*set_info_proc)(struct xdffile*, enum xdffield, ...); 
	int (*get_info_proc)(struct xdffile*, enum xdffield, ...); 
	int (*copy_info_proc)(struct xdffile*, struct xdffile*); 
	int (*write_header_proc)(struct xdffile*);
	int (*close_file_proc)(struct xdffile*);
	
	/* Background thread synchronization object */
	pthread_t thid;
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	sem_t sem;
	int order;
};

#endif /* XDFFILE_H */
