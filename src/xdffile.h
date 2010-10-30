/*
	Copyright (C) 2010  EPFL (Ecole Polytechnique Fédérale de Lausanne)
	Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef XDFFILE_H
#define XDFFILE_H


#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <time.h>

#include "xdfio.h"

#define TYPE_INT		0
#define TYPE_STRING		1
#define TYPE_DATATYPE		2
#define TYPE_DOUBLE		3
#define TYPE_TIME_T		4
#define TYPE_UINT		5
#define TYPE_3DPOS		6
#define TYPE_ICD		7

union optval {
	int i;
	const char* str;
	enum xdftype type;
	double d;
	time_t ts;
	unsigned int ui;
	char icd[6];
	double pos[3];
};

struct format_operations {
	int (*set_channel)(struct xdfch*, enum xdffield,
	                   union optval, int);
	int (*get_channel)(const struct xdfch*, enum xdffield,
	                   union optval*, int);
	int (*set_conf)(struct xdf*, enum xdffield, union optval, int); 
	int (*get_conf)(const struct xdf*, enum xdffield,
	                union optval*, int); 
	int (*write_header)(struct xdf*);
	int (*read_header)(struct xdf*);
	int (*complete_file)(struct xdf*);
	enum xdffiletype type;
	bool supported_type[XDF_NUM_DATA_TYPES];
	int choff, fileoff;
	size_t chlen, filelen;
	const enum xdffield* chfields;
	const enum xdffield* filefields;
};

struct xdfch {
	int iarray, offset, digital_inmem;
	enum xdftype inmemtype, infiletype;
	double physical_mm[2], digital_mm[2];
	struct xdfch* next;
	struct xdf* owner;
};

struct xdf {
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
	size_t* array_stride;	

	struct eventtable* table;

	/* Data format specific behavior */
	struct xdfch* defaultch;
	const struct format_operations* ops;
	
	/* Background thread synchronization object */
	pthread_t thid;
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	sem_t sem;
	int order;
};

XDF_LOCAL enum xdffiletype xdf_guess_filetype(const unsigned char* magickey);
XDF_LOCAL struct xdf* xdf_alloc_file(enum xdffiletype type);
XDF_LOCAL struct xdfch* xdf_alloc_channel(struct xdf* owner);
XDF_LOCAL int xdf_set_error(int error);


#endif /* XDFFILE_H */
