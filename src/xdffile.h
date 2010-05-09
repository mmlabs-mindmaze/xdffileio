#ifndef XDFFILE_H
#define XDFFILE_H


#include <pthread.h>
#include <semaphore.h>
#include "xdfformatops.h"

struct xdf_channel {
	unsigned int iarray, offset;
	enum xdftype inmemtype, infiletype;
	double physical_mm[2], digital_mm[2];
	struct xdf_channel* next;
	const struct format_operations* ops;
};

struct xdffile {
	enum xdffiletype ftype;
	int fd;					
	unsigned int ready, mode;			
	long pointer;			
	double rec_duration;
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
	const struct format_operations* ops;
	
	/* Background thread synchronization object */
	pthread_t thid;
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	sem_t sem;
	int order;
};

#endif /* XDFFILE_H */
