#ifndef XDFFILE_H
#define XDFFILE_H


#include <pthread.h>
#include <semaphore.h>
#include "xdfdatatypes.h"


struct xdf_channel {
	unsigned int iarray, offset;
	enum xdftype inmemtype, infiletype;
	double physical_mm[2], digital_mm[2];
	struct xdf_channel* next;
};

struct xdffile {
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
	
	

	/* Background thread synchronization object */
	pthread_t thid;
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	sem_t sem;
	int order;
};

int xdf_define_arrays(struct xdffile* xdf, unsigned int numarrays, unsigned int* strides);
int xdf_add_channel(struct xdffile* xdf, const struct xdf_channel* channel);
int xdf_prepare_transfer(struct xdffile* xdf);
int xdf_write(struct xdffile* xdf, unsigned int ns, ...);
int xdf_read(struct xdffile* xdf, unsigned int ns, void* samples);

#endif /* XDFFILE_H */
