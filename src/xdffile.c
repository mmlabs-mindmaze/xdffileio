/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Copyright (C) 2013  Nicolas Bourdaud

    Authors:
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <mmthread.h>
#include <mmsysio.h>

#include "xdfio.h"
#include "xdftypes.h"
#include "xdffile.h"
#include "xdfevent.h"

/***************************************************
 *                Local declarations               *
 ***************************************************/

#if !defined(_WIN32)
#include <pthread.h>
static void block_signals(sigset_t *oldmask)
{
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGXFSZ);
	pthread_sigmask(SIG_BLOCK, &mask, oldmask);
}

static void unblock_signals(sigset_t *oldmask)
{
	pthread_sigmask(SIG_SETMASK, oldmask, NULL);
}

#else
#ifndef sigset_t
#define sigset_t long
#endif /* sigset_t */

static void block_signals(sigset_t *oldmask)
{
	(void)oldmask;
}
static void unblock_signals(sigset_t *oldmask)
{
	(void)oldmask;
}

#endif /* _WIN32 */


// Orders definitions for transfer
#define ORDER_INIT	3
#define ORDER_QUIT	2
#define ORDER_TRANSFER	1
#define ORDER_NONE	0


struct data_batch {
	int len;
	int iarray;
	int foff, moff;
};

struct convertion_data {
	struct convprm prm;
	unsigned int filetypesize, memtypesize;
	int skip;
	int buff_offset;
};

struct ch_array_map {
	int index;
	const struct xdfch* ch;
	struct data_batch batch;
};

/**************************************************
 *                 misc functions                 *
 **************************************************/

/**
 * xdf_set_error() - sets errno
 * @error: error to which errno is set
 *
 * Return: 0 if no error is set, -1 if an error is set
 */
LOCAL_FN int xdf_set_error(int error)
{
	if (error) {
		errno = error;
		return -1;
	}
	return 0;
}

/***************************************************
 *        Transfer thread related functions        *
 ***************************************************/

/* \param xdf	pointer to a valid xdffile with mode XDF_WRITE
 * 
 * Transpose recorded data from (channel,sample) to a (sample,channel)
 * organisation, performs any necessary conversion and write the record on
 * the file.
 *
 * it updates xdf->reportval by negative values (-errno) for error
 */
static int write_diskrec(struct xdf* xdf)
{
	ssize_t wsize;
	size_t reqsize;
	unsigned ich;
	struct convertion_data* ch;
	char *fbuff, *src;
	char* srcbase = xdf->backbuff;
	void* dst = xdf->tmpbuff[0];
	void* buff = xdf->tmpbuff[1];

	// Transfer, convert and copy to file each channel data
	for (ich = 0; ich < xdf->numch; ich++) {
		ch = xdf->convdata + ich;

		// Convert data
		src = srcbase + ch->buff_offset;
		xdf_transconv_data(xdf->ns_per_rec, dst, src, &(ch->prm), buff);

		// Write the converted data to the file. Continue writing
		// as long as not all data has been written
		reqsize = xdf->ns_per_rec * ch->filetypesize;
		fbuff = dst;
		do {
			wsize = mm_write(xdf->fd, fbuff, reqsize);
			if (wsize == -1) { 
				xdf->reportval = -errno;
				return -1;
			}
			reqsize -= wsize;
			fbuff += wsize;
		} while (reqsize);
	}

	// Make sure that the whole record has been sent to hardware
	if (mm_fsync(xdf->fd)) {
		xdf->reportval = -errno;
		return -1;
	}
	xdf->nrecord++;

	return 0;
}

/* \param xdf	pointer to a valid xdffile with mode XDF_READ
 * 
 * Transpose recorde data from (sample,channel) to a (channel,sample)
 * organisation, performs any necessary conversion and write the record on
 * the file.
 *
 * it updates xdf->reportval:
 *     - 1 if end of file is reached
 *     - negative values (-errno) for error
 */
static int read_diskrec(struct xdf* xdf)
{
	ssize_t rsize;
	size_t reqsize;
	unsigned int ich;
	struct convertion_data* ch;
	char *fbuff, *dst;
	char* dstbase = xdf->backbuff;
	void* src = xdf->tmpbuff[0];
	void* buff = xdf->tmpbuff[1];

	// Transfer, convert and copy to file each channel data
	for (ich = 0; ich < xdf->numch; ich++) {
		ch = xdf->convdata + ich;

		// Read the data from file. Continue reading
		// as long as not all data has been read
		reqsize = xdf->ns_per_rec * ch->filetypesize;
		fbuff = src;
		do {
			rsize = mm_read(xdf->fd, fbuff, reqsize);
			if ((rsize == 0) || (rsize == -1)) {
				xdf->reportval = (rsize == 0) ? 1 : -errno;
				return -1;
			}
			reqsize -= rsize;
			fbuff += rsize;
		} while (reqsize);

		if (ch->skip) 
			continue;
		// Convert data if the it will be sent to one of the arrays
		dst = dstbase + ch->buff_offset;
		xdf_transconv_data(xdf->ns_per_rec, dst,
		                   src, &(ch->prm), buff);
	}

	return 0;
}


/* \param ptr	pointer to a valid xdffile structure
 *
 * This is the function implementing the background thread transferring data
 * from/to the underlying file. 
 * This performs the transfer of the back buffer whenever the condition is
 * signaled and order is ORDER_TRANSFER. The end of the transfer is notified
 * by clearing the order and signal the condition
 *
 * This function reports information back to the main thread using
 * xdf->reportval which is updated by read_diskrec and write_diskrec when
 * necessary
 */
static void* transfer_thread_fn(void* ptr)
{
	struct xdf* xdf = ptr;
	int wmode = (xdf->mode == XDF_WRITE) ? 1 : 0;

	block_signals(NULL);
	
 	// While a transfer is performed, this routine holds the mutex
	// preventing from any early buffer swap
 	mm_thr_mutex_lock(&(xdf->mtx));
	while (1) {
		// The transfer ready to be performed
		// => clear the previous order and notify the main thread
		xdf->order = 0;
		mm_thr_cond_signal(&(xdf->cond));

		// Wait for an order of transfer
		while (!xdf->order)
			mm_thr_cond_wait(&(xdf->cond), &(xdf->mtx));
	
		// break the transfer loop if the quit order has been sent
		if (xdf->order == ORDER_QUIT)
			break;

		// Write/Read a record
		if (wmode)
			write_diskrec(xdf);
		else
			read_diskrec(xdf);

	}
	mm_thr_mutex_unlock(&(xdf->mtx));
	return NULL;
}


/* \param xdf	pointer to a valid xdffile structure
 *
 * Notify the background thread that a record has to be written or read,
 * depending on the mode of the xdf structure. This function will block if
 * the previous transfer is still being performed.
 *
 * It inspects also the information reported by the transfer thread
 *
 * In addition to usual error reporting (0 if success, -1 if error), it
 * returns 1 if the transfer thread has reported end of file.
 */
static int disk_transfer(struct xdf* xdf)
{
	int retval = 0;
	void* buffer;

	// If the mutex is hold by someone else, it means that the transfer
	// routine has still not in a ready state
	mm_thr_mutex_lock(&(xdf->mtx));

	// Wait for the previous operation to be finished
	while (xdf->order && !xdf->reportval)
		mm_thr_cond_wait(&(xdf->cond), &(xdf->mtx));

	if (xdf->reportval) {
		if (xdf->reportval < 0) {
			errno = -xdf->reportval;
			retval = -1;
		} else
			retval = 1;
	} else {
		// Swap front and back buffer
		buffer = xdf->backbuff;
		xdf->backbuff = xdf->buff;
		xdf->buff = buffer;

		// Signal for data transfer
		xdf->order = ORDER_TRANSFER;
		mm_thr_cond_signal(&(xdf->cond));
	}

	// We are safe now, the transfer can start from now
	mm_thr_mutex_unlock(&(xdf->mtx));

	return retval;
}


/***************************************************
 *           Batch preparation functions           *
 ***************************************************/


/* \param a     pointer to first struct ch_array_map pointer element
 * \param b     pointer to second struct ch_array_map pointer element
 *
 * This implements the order comparison between 2 elements of struct
 * ch_array_map. The order is first determined by index of the array whose
 * channel maps its data and then the offset within this array.
 *
 * Returns an integer less than, equal to, or greater than zero if the first
 * argument is considered to be respectively less than, equal to, or greater
 * than the second.
 */
static
int ch_array_map_cmp(const void* a, const void* b)
{
	const struct ch_array_map* map1 = a;
	const struct ch_array_map* map2 = b;

	if (map1->batch.iarray != map2->batch.iarray)
		return map1->batch.iarray - map2->batch.iarray;

	return map1->batch.moff - map2->batch.moff;
}


/* \param xdf   pointer of a valid xdf file
 * \param map   array of struct ch_array_map element. It must be of the length
 *              of the number of channel in xdf
 *
 * Initial map with the content of channel. This determines for each channel a
 * suitable mapping between arrays and memory offset within transfer buffer.
 *
 * Return the sample_size (stride) of the transfer buffer.
 */
static
size_t init_ch_array_mapping(struct xdf* xdf, struct ch_array_map* map)
{
	const struct xdfch* ch;
	size_t sample_size;
	int nch = xdf->numch;
	int i;

	// Initialize the map element in the same order as the channels
	ch = xdf->channels;
	for (i = 0; i < nch; i++) {
		map[i] = (struct ch_array_map) {
			.index = i,
			.ch = ch,
			.batch = {
				.moff = ch->offset,
				.iarray = ch->iarray,
				.len = xdf_get_datasize(ch->inmemtype),
			},
		};
		ch = ch->next;
	}

	// reorder ch_array_mapping according to iarray first then array offset.
	qsort(map, nch, sizeof(map[0]), ch_array_map_cmp);

	// Allocate offset for each channel within the transfer buffer
	sample_size = 0;
	for (i = 0; i < nch; i++) {
		map[i].batch.foff = sample_size;
		if (map[i].batch.iarray >= 0)
			sample_size += map[i].batch.len;
	}

	return sample_size;
}



/* \param nch   number of channels
 * \param map   array of channel array mapping (length: nch)
 *
 * Given an initialized (and ordered) array of channel-array mapping, determine
 * the number of batches to copy the data in array to/from the transfer buffer.
 *
 * After execution of this function, the batches will be set in the .batch
 * field of the first element of map (up the determined number of batch).
 *
 * Return the number of batches
 */
static
int link_batches(int nch, struct ch_array_map* map)
{
	int i, nbatch;
	struct data_batch* last;

	// Init first batch to the first channel mapped to an array
	nbatch = 0;
	last = &map[0].batch;
	for (i = 0; i < nch; i++) {
		if (map[i].batch.iarray >= 0) {
			nbatch = 1;
			*last = map[i].batch;
			i++;  // the next loop will start from the next index
			break;
		}
	}

	// The batches are already ordered by array then offset. Merging is then
	// only checked if batches have same array and have consecutive mapping
	for (; i < nch; i++) {
		// test if incoming raw batch can be merged in last batch
		if (last->iarray == map[i].batch.iarray
		   && last->moff + last->len == map[i].batch.moff
		   && last->foff + last->len == map[i].batch.foff) {
			// do merge: extent last batch size
			last->len += map[i].batch.len;
		} else {
			// cannot merge: create a new batch from the raw batch
			last = &map[nbatch++].batch;
			*last = map[i].batch;
		}
	}

	return nbatch;
}


/***************************************************
 *          Transfer preparation functions         *
 ***************************************************/

/* \param xdf           pointer of a valid xdf file
 * \param nbatch        computer number of batches
 * \param sample_size   size in byte of a sample in transfer buffer
 *
 * Allocate all the buffers and temporary objects needed for the transfer
 */
static
int alloc_transfer_objects(struct xdf* xdf, int nbatch, size_t sample_size)
{
	xdf->sample_size = sample_size;
	xdf->nbatch = nbatch;

	if ( !(xdf->convdata = malloc(xdf->numch*sizeof(*(xdf->convdata))))
	    || !(xdf->batch = malloc(xdf->nbatch*sizeof(*(xdf->batch))))
	    || !(xdf->buff = malloc(sample_size * xdf->ns_per_rec))
	    || !(xdf->backbuff = malloc(sample_size * xdf->ns_per_rec))
	    || !(xdf->tmpbuff[0] = malloc(xdf->ns_per_rec * 8))
	    || !(xdf->tmpbuff[1] = malloc(xdf->ns_per_rec * 8)) ) {
		return -1;
	}

	return 0;
}


/* \param xdf	pointer of a valid xdf file
 *
 * Free the buffers and temporary objects needed for the transfer. It will
 * reset all value to NULL so that the function can be recalled safely.
 */
static void free_transfer_objects(struct xdf* xdf)
{
	free(xdf->convdata);
	free(xdf->batch);
	free(xdf->buff);
	free(xdf->backbuff);
	free(xdf->tmpbuff[0]);
	free(xdf->tmpbuff[1]);
	xdf->convdata = NULL;
	xdf->batch = NULL;
	xdf->buff = xdf->backbuff = NULL;
	xdf->tmpbuff[0] = xdf->tmpbuff[1] = NULL;

	xdf->nbatch = 0;
}


/* \param nch   number of channels
 * \param sample_size   size in byte the a sample in a transfer buffer
 * \param mode  XDF_READ or XDF_WRITE
 * \param map   array of channel array mapping (length: nch)
 * \param convdata_array  array of convertion_data to initialize
 *
 * Setup the parameters of conversion of each channels in the xDF file.
 */
static
void setup_convdata(int nch, size_t sample_size, int mode,
                    const struct ch_array_map* map,
                    struct convertion_data* convdata_array)
{
	int i, idx, in_str, out_str;
	enum xdftype in_tp, out_tp;
	const double *in_mm, *out_mm;
	int swaptype;
	const struct xdfch* ch;
	struct convertion_data* convdata;

	for (i = 0; i < nch; i++) {
		idx = map[i].index;
		ch = map[i].ch;
		convdata = &convdata_array[idx];

		if (mode == XDF_WRITE) {
			// In write mode, conversion in
			// from mem/physical to file/digital
			in_tp = ch->inmemtype;
			in_str = sample_size;
			in_mm = ch->physical_mm;
			out_tp = ch->infiletype;
			out_str = xdf_get_datasize(out_tp);
			out_mm = ch->digital_mm;
			swaptype = SWAP_OUT;
		} else {
			// In read mode, conversion in
			// from file/digital to mem/physical
			in_tp = ch->infiletype;
			in_str = xdf_get_datasize(in_tp);
			in_mm = ch->digital_mm;
			out_tp = ch->inmemtype;
			out_str = sample_size;
			out_mm = ch->physical_mm;
			swaptype = SWAP_IN;
		}
			
		// If data manipulated in memory is digital => no scaling
		if (ch->digital_inmem)
			in_mm = out_mm = NULL;
		
		*convdata = (struct convertion_data) {
			.skip = (ch->iarray < 0),
			.buff_offset = map[i].batch.foff,
			.filetypesize = xdf_get_datasize(ch->infiletype),
			.memtypesize = xdf_get_datasize(ch->inmemtype),
		};
		xdf_setup_transform(&convdata->prm, swaptype,
		                in_str, in_tp, in_mm,
		                out_str, out_tp, out_mm);
	}
}


/* \param xdf	pointer of a valid xdf file
 *
 * Compute and return the size in byte of a record in the file
 */
static
size_t compute_filerec_size(struct xdf* xdf)
{
	size_t filerec_size_per_sample;
	const struct xdfch* ch;

	filerec_size_per_sample = 0;
	ch = xdf->channels;
	while (ch) {
		filerec_size_per_sample += xdf_get_datasize(ch->infiletype);
		ch = ch->next;
	}

	return filerec_size_per_sample * xdf->ns_per_rec;
}


static
int setup_transfer_objects(struct xdf* xdf)
{
	int nch = xdf->numch;
	struct ch_array_map mapping[nch];
	struct convertion_data convdata[nch];
	size_t sample_size;
	int i, nbatch;

	sample_size = init_ch_array_mapping(xdf, mapping);
	setup_convdata(nch, sample_size, xdf->mode, mapping, convdata);
	nbatch = link_batches(nch, mapping);

	// Alloc of entities needed for conversion
	if (alloc_transfer_objects(xdf, nbatch, sample_size))
		return -1;

	for (i = 0; i < nbatch; i++)
		xdf->batch[i] = mapping[i].batch;

	for (i = 0; i < nch; i++)
		xdf->convdata[i] = convdata[i];

	xdf->filerec_size = compute_filerec_size(xdf);
	return 0;
}


/* \param xdf	pointer of a valid xdf file
 *
 * Initialize the synchronization primitives and start the transfer thread.
 */
static int init_transfer_thread(struct xdf* xdf)
{
	int ret;
	int done = 0;

	if ((ret = mm_thr_mutex_init(&(xdf->mtx), 0)))
		goto error;
	done++;

	if ((ret = mm_thr_cond_init(&(xdf->cond), 0)))
		goto error;
	done++;

	xdf->reportval = 0;
	xdf->order = ORDER_INIT;
	if ((ret = mm_thr_create(&(xdf->thid), transfer_thread_fn, xdf)))
		goto error;

	return 0;

error:
	if (done-- > 0)
		mm_thr_cond_deinit(&(xdf->cond));
	if (done-- > 0)
		mm_thr_mutex_deinit(&(xdf->mtx));
	errno = ret;
	return -1;
}


/* \param xdf	pointer of a valid xdf file
 *
 * Order the thread to finish, wait for it and free the synchronization
 * primitives.
 */
static int finish_transfer_thread(struct xdf* xdf)
{

	// Wait for the previous operation to be finished
	// and stop the transfer thread
	mm_thr_mutex_lock(&(xdf->mtx));
	while (xdf->order && !xdf->reportval)
		mm_thr_cond_wait(&(xdf->cond), &(xdf->mtx));
	xdf->order = ORDER_QUIT;
	mm_thr_cond_signal(&(xdf->cond));
	mm_thr_mutex_unlock(&(xdf->mtx));

	// Wait for the transfer thread to complete
	mm_thr_join(xdf->thid, NULL);

	// Destroy synchronization primitives
	mm_thr_mutex_deinit(&(xdf->mtx));
	mm_thr_cond_deinit(&(xdf->cond));

	return 0;
}


/* \param xdf	pointer of a valid xdf file with mode XDF_WRITE
 *
 * Fill the remaining of the current record with 0 and transfer it. This
 * ensures that no previous data will added be truncated because of the end.
 */
static int finish_record(struct xdf* xdf)
{
	char* buffer = xdf->buff + xdf->sample_size * xdf->ns_buff;
	unsigned int ns = xdf->ns_per_rec - xdf->ns_buff;

	if (!xdf->ns_buff)
		return 0;

	// Fill the remaining of the record with 0 values
	while (ns--) {
		memset(buffer, 0, xdf->sample_size);
		buffer += xdf->sample_size;
	}

	return disk_transfer(xdf);
}


/* \param xdf	pointer of a valid xdf file with mode XDF_WRITE
 *
 * Write the header of the file format
 */
static int init_file_content(struct xdf* xdf)
{
	int retval = 0;
	sigset_t oldmask;
	
	block_signals(&oldmask);
	if (xdf->ops->write_header(xdf) || mm_fsync(xdf->fd))
		retval = -1;
	else
		xdf->nrecord = 0;
	
	unblock_signals(&oldmask);
	return retval;
}


/* \param xdf 	pointer to a valid xdffile with mode XDF_WRITE 
 *
 * Finish the file
 */
static int complete_file_content(struct xdf* xdf)
{
	int retval = 0;
	sigset_t oldmask;

	block_signals(&oldmask);
	if (xdf->ops->complete_file(xdf) || mm_fsync(xdf->fd))
		retval = -1;

	unblock_signals(&oldmask);

	return retval;
}


/* \param xdf 	pointer to a valid xdffile with mode XDF_WRITE 
 * \param ns	number of samples to be added
 * \param vbuff list of pointers to the arrays holding the input samples
 *
 * Add samples coming from one or several input arrays containing the
 * samples. The number of arrays that must be provided on the call depends
 * on the specification of the channels.
 * Returns the number of samples written, -1 in case of error
 */
static int writev_buffers(struct xdf* xdf, size_t ns,
                              const char* restrict *in)
{
	unsigned int i, k, ia, nsrec = xdf->ns_per_rec;
	unsigned int nbatch = xdf->nbatch, samsize = xdf->sample_size;
	char* restrict buff = xdf->buff + samsize * xdf->ns_buff;
	struct data_batch* batch = xdf->batch;
	const void* arr_ptr;

	for (i=0; i<ns; i++) {
		// Write the content of the buffer if full
		if (xdf->ns_buff == nsrec) {
			if (disk_transfer(xdf)) 
				return (i==0) ? -1 : (int)i;
			buff = xdf->buff;
			xdf->ns_buff = 0;
		}

		// Transfer the sample to the buffer by chunk
		for (k=0; k<nbatch; k++) {
			ia = batch[k].iarray;
			arr_ptr = in[ia] + batch[k].moff;
			memcpy(buff+batch[k].foff, arr_ptr, batch[k].len);
		}
		buff += samsize;
		xdf->ns_buff++;
		for (ia = 0; ia < xdf->narrays; ia++)
			in[ia] += xdf->array_stride[ia];
	}

	return (int)ns;
}


/* \param xdf 	pointer to a valid xdffile with mode XDF_READ 
 * \param ns	number of samples to be read
 * \param vbuff list of pointers to the arrays holding the output samples
 *
 * Read samples in the buffer and transfer them to one or several output
 * arrays. The number of arrays that must be provided on the call depends
 * on the specification of the channels.
 * Returns the number of samples read, -1 in case of error
 */
static int readv_buffers(struct xdf* xdf, size_t ns, char* restrict* out)
{
	unsigned int i, k, ia;
	unsigned int nbatch = xdf->nbatch, samsize = xdf->sample_size;
	char* restrict buff = xdf->buff + samsize * (xdf->ns_per_rec-xdf->ns_buff);
	struct data_batch* batch = xdf->batch;
	int ret;
	void* arr_ptr;

	for (i=0; i<ns; i++) {
		// Trigger a disk read when the content of buffer is empty
		if (!xdf->ns_buff) {
			if ((ret = disk_transfer(xdf))) 
				return ((ret<0)&&(i==0)) ? -1 : (int)i;
			buff = xdf->buff;
			xdf->ns_buff = xdf->ns_per_rec;
			xdf->nrecread++;
		}

		// Transfer the sample to the buffer by chunk
		for (k=0; k < nbatch; k++) {
			ia = batch[k].iarray;
			arr_ptr = out[ia] + batch[k].moff;
			memcpy(arr_ptr, buff+batch[k].foff, batch[k].len);
		}
		buff += samsize;
		xdf->ns_buff--;
		for (ia = 0; ia < xdf->narrays; ia++)
			out[ia] += xdf->array_stride[ia];
	}

	return (int) ns;
}


static
void remove_tmp_event_files(struct xdf* xdf)
{
	size_t len;

	if (xdf == NULL || xdf->filename == NULL)
		return;

	len = strlen(xdf->filename);

	/* room for the suffix has been ensured during opening */
	strcat(xdf->filename, ".event");
	mm_close(xdf->tmp_event_fd);
	remove(xdf->filename);

	xdf->filename[len] = '\0';
	strcat(xdf->filename, ".code");
	mm_close(xdf->tmp_code_fd);
	remove(xdf->filename);
}


/***************************************************
 *                   API functions                 *
 ***************************************************/

/**
 * xdf_close() - closes a xDF file
 * @xdf: pointer to a valid xdf file
 *
 * xdf_close() closes the xDF file referenced by the handle @xdf. When
 * the file is closed, if a record is not full, it will be completed by zeros.
 * After a call to xdf_close(), @xdf should not be used anymore even
 * if the call fails since all resources associated will be freed anyways.
 *
 * Return: 
 * 0 in case of success. Otherwise, -1 is returned and errno is set
 * accordingly.
 *
 * Errors:
 * EINVAL
 *   @xdf is NULL
 *
 * EFBIG
 *   an attempt was made to write a file that exceeds the
 *   implementation-defined maximum file size or the process's file size
 *   limit, or to write at a position past the maximum allowed offset
 *
 * EINTR
 *   the call was interrupted by a signal before any data was written
 *
 * EIO
 *   a low-level I/O error occurred while modifying the inode
 *
 * ENOSPC
 *   the device containing the xDF file has no room for the data,
 *   or ESTALE if stale file handle. This error can occur for NFS and
 *   for other file systems
 */
API_EXPORTED int xdf_close(struct xdf* xdf)
{
	int retval = 0;
	struct xdfch *ch, *prev;

	if (!xdf)
		return xdf_set_error(EINVAL);

	if (xdf->ready) {
		if ((xdf->mode == XDF_WRITE) && finish_record(xdf))
			retval = -1;
		
		finish_transfer_thread(xdf);
		free_transfer_objects(xdf);

		if ((xdf->mode == XDF_WRITE) && complete_file_content(xdf))
			retval = -1;
	}

	if ((xdf->fd >= 0) && xdf->closefd_ondestroy && mm_close(xdf->fd))
		retval = -1;

	// Free channels and file
	free(xdf->array_stride);
	destroy_event_table(xdf->table);

	/* destroy temporary event and code files */
	if (retval == 0)
		remove_tmp_event_files(xdf);

	free(xdf->filename);

	ch=xdf->channels;
	while (ch) {
		prev = ch;
		ch = ch->next;
		free((char*)prev - xdf->ops->choff);
	}
	free((char*)xdf - xdf->ops->fileoff);

	return retval;
}


/**
 * xdf_define_arrays() - specifies the number of input/output arrays
 * @xdf: pointer to a valid xdf file
 * @numarrays: number of arrays that will be used
 * @strides: strides for each arrays
 *
 * xdf_define_arrays() specifies the number of arrays and its strides for
 * upcoming calls to xdf_write() and xdf_read() using @xdf.
 * This function is used to configure the upcoming transfer. As such, it must
 * be called before xdf_prepare_transfer() (calling it after will
 * produce an error). However the function may be called several times but
 * only the last call is meaningful for the transfer.
 *
 * @numarrays specifies the number of arrays that will be provided in the
 * argument list of xdf_write() or xdf_read().
 *
 * @strides argument should point to an array of @numarrays unsigned
 * int corresponding respectively to the stride of each arrays that will be
 * supplied to the upcoming call to xdf_write() or xdf_read().
 * The stride corresponds to the length in byte between two consecutive
 * samples of the same channel in the array.
 *
 * Return:
 * 0 in case of success, otherwise -1 and errno is set 
 * appropriately.
 *
 * Errors:
 * ENOMEM
 *   the system is unable to allocate resources
 */
API_EXPORTED int xdf_define_arrays(struct xdf* xdf, unsigned int numarrays, const size_t* strides)
{
	size_t* newstrides;
	if (!(newstrides = malloc(numarrays*sizeof(*(xdf->array_stride))))) 
		return -1;

	free(xdf->array_stride);
	xdf->array_stride = newstrides;
	xdf->narrays = numarrays;
	memcpy(xdf->array_stride, strides, numarrays*sizeof(*(xdf->array_stride)));

	return 0;
}


/**
 * xdf_prepare_transfer() - sets up the internals of the XDF file to be ready
 *                          to receive or send data.
 * @xdf: pointer to a valid xdf file
 *
 * xdf_prepare_transfer() sets up the internal structures of the XDF file
 * referenced by @xdf to be ready to receive or send data. After a
 * successful call to it, you can call xdf_write() or xdf_read()
 * depending of the mode of the XDF file.
 *
 * Since this function prepares the transfer, no call to any function which
 * configures it will be allowed anymore after xdf_define_arrays()
 * succeed. In particular, xdf_set_conf(), xdf_setchconf() and
 * xdf_define_arrays() will fail afterwards.
 *
 * In case of failure due to I/O (file to big, connection to file system
 * lost...), the best procedure is to close the file since the underlying file
 * will be in a undertermined stated.
 *
 * Return: 
 * 0 in case of success. Otherwise -1 is returned and errno is set
 * appropriately.
 *
 * Errors:
 * EINVAL
 *   @xdf is NULL
 *
 * ENOMEM
 *   the system is unable to allocate memory resources
 *
 * EFBIG
 *   an attempt was made to write a file that exceeds the
 *   implementation-defined maximum file size or the process's file size
 *   limit, or to write at a position past the maximum allowed offset
 *
 * EINTR
 *   the call was interrupted by a signal before any data was written; 
 *   see signal()
 *
 * EIO
 *   a low-level I/O error occurred while modifying the inode
 *
 * ENOSPC
 *   the device containing the xDF file has no room for the data
 *
 * ESTALE
 *   stale file handle, this error can occur for NFS and for other file
 *   systems
 */
API_EXPORTED int xdf_prepare_transfer(struct xdf* xdf)
{
	if (xdf->ready)
		return -1;

	if (setup_transfer_objects(xdf))
		goto error;

	if (xdf->mode == XDF_WRITE) {
		if (init_file_content(xdf))
			goto error;
	}

	if (init_transfer_thread(xdf))
		goto error;

	if (xdf->mode == XDF_READ) {
		disk_transfer(xdf);
		xdf->nrecread = -1;
		xdf->ns_buff = 0;
	}

	xdf->ready = 1;
	return 0;

error:
	free_transfer_objects(xdf);
	return -1;
}

/**
 * xdf_end_transfer() - cleans up the internals of the XDF file that was
 *                      ready to receive or send data.
 * @xdf: pointer to a valid xdf file
 *
 * xdf_end_transfer() is the opposite of xdf_prepare_transfer(): it
 * resets the xdf file to the state where it can be reconfigured for reading.
 *
 * Return:
 * 0 in case of success. Otherwise -1 is returned and errno is set
 * appropriately.
 *
 * Errors:
 * EINVAL
 *   @xdf is NULL
 *
 * ENOMEM
 *   the system is unable to allocate memory resources
 *
 * EFBIG
 *   an attempt was made to write a file that exceeds the
 *   implementation-defined maximum file size or the process's file size
 *   limit, or to write at a position past the maximum allowed offset
 *
 * EINTR
 *   the call was interrupted by a signal before any data was written; 
 *   see signal()
 *
 * EIO
 *   a low-level I/O error occurred while modifying the inode
 *
 * ENOSPC
 *   the device containing the xDF file has no room for the data
 *
 * ESTALE
 *   stale file handle. This error can occur for NFS and for other file systems
 */
API_EXPORTED int xdf_end_transfer(struct xdf* xdf)
{
	mm_off_t pos;

	if (xdf == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (xdf->ready == 0)
		return 0;

	finish_transfer_thread(xdf);
	free_transfer_objects(xdf);
	xdf->nbatch = 0;
	xdf->ready = 0;

	pos = mm_seek(xdf->fd, xdf->hdr_offset, SEEK_SET);
	return (pos < 0) ? -1 : 0;
}

/**
 * xdf_write() - writes samples to a XDF file
 * @xdf: pointer to a valid xdffile with mode XDF_WRITE
 * @ns: number of samples to be added
 *
 * xdf_write() writes @ns samples to the XDF file referenced by
 * @xdf. This file should have been opened with mode XDF_WRITE and
 * xdf_prepare_transfer() should have been successfully called on it.
 * xdf_write() will fail otherwise).
 *
 * The data to be added should be contained in arrays specified by pointers
 * provided in the variable list of arguments of the function.  The function
 * expects the same number of arrays as specified by previous call to
 * xdf_define_arrays(). The internal organisation of the data in the
 * arrays should have been specified previously with calls to
 * xdf_set_chconf().
 *
 * In addition, it is important to note that none of the arrays should overlap.
 *
 * Performance: By design of the library, a call to xdf_write() is almost
 * ensured to be executed in a linear time, i.e. given a fixed configuration
 * of an XDF file, for the same number of samples to be passed, a call
 * xdf_write will almost take always the same time to complete. This time
 * increases linearly with the number of samples. This insurance is particularly
 * useful for realtime processing of data, since storing the data will impact
 * the main loop in a predictable way.
 *
 * This is achieved by double buffering the data for writing. A front and a
 * back buffer are available: the front buffer is filled with the incoming
 * data, and swapped with the back buffer when full. This swap signals a
 * background thread to convert, reorganise, scale and save to the disk the
 * data contained in the full buffer making it afterwards available for the
 * next swap.
 *
 * This approach ensures a linear calltime of xdf_write() providing that I/O
 * subsystem is not saturated neither all processing units (cores or
 * processors), i.e. the application is neither I/O bound nor CPU bound.
 *
 * Data safety: The library makes sure that data written to XDF files are
 * safely stored on stable storage on a regular basis but because of double
 * buffering, there is a risk to loose data in case of problem. However, the
 * design of the xdf_write() ensures that if a problem occurs (no more disk
 * space, power supply cut), at most two records of data plus the size
 * of the chunks of data supplied to the function will be lost.
 *
 * As an example, assuming you record a XDF file at 256Hz using records of 256
 * samples and you feed xdf_write() with chunks of 8 samples, you are
 * ensured to receive notification of failure after at most 520 samples
 * corresponding to a lose of at most a little more than 2s of data in case of
 * problems.
 *
 * Return: 
 * the number of the samples successfully added to the XDF file in
 * case of success. Otherwise -1 is returned and errno is set
 * appropriately.
 *
 * Errors:
 * EINVAL
 *   @xdf is NULL
 *
 * EPERM
 *   no successful call to xdf_prepare_transfer() have been done on @xdf or
 *   it has been opened using the mode XDF_READ
 *
 * EFBIG
 *   an attempt was made to write a file that exceeds the
 *   implementation-defined maximum file size or the process's file size
 *   limit, or to write at a position past the maximum allowed offset
 *
 * EINTR
 *   the call was interrupted by a signal before any data was written,
 *   see signal()
 *
 * EIO
 *   a low-level I/O error occurred while modifying the inode
 *
 * ENOSPC
 *   the device containing the xDF file has no room for the data
 *
 * ESTALE
 *   stale file handle. This error can occur for NFS and for other file systems
 *
 *
 * Example:
 *    // Assume xdf references a xDF file opened for writing whose
 *    // channels source their data in 2 arrays of float whose strides
 *    // are the length of respectively 4 and 6 float values,
 *    // i.e. 16 and 24 bytes (in most platforms)
 *    #define NS    3
 *    float array1[NS][4], array2[NS][6];
 *    unsigned int strides = {4*sizeof(float), 6*sizeof(float)};
 *    unsigned int i;
 *
 *    xdf_define_arrays(xdf, 2, strides);
 *    if (xdf_prepare_transfer(xdf)
 * 	   return 1;
 *
 *    for (i=0; i<45; i+=NS) {
 * 	   // Update the values contained in array1 and array2
 *	   ...
 *
 * 	   // Write the values to the file
 *         if (xdf_write(xdf, NS, array1, array2))
 *		   return 1;
 *    }
 *
 *    xdf_close(xdf);
 */
API_EXPORTED int xdf_write(struct xdf* xdf, size_t ns, ...)
{
	if ((xdf == NULL) || !xdf->ready || (xdf->mode == XDF_READ)) {
		errno = (xdf == NULL) ? EINVAL : EPERM;
		return -1;
	}

	unsigned int i;
	const char* restrict in[xdf->narrays];
	va_list ap;

	// Initialization of the input buffers
	va_start(ap, ns);
	for (i=0; i<xdf->narrays; i++)
		in[i] = va_arg(ap, const char*);
	va_end(ap);

	return writev_buffers(xdf, ns, in);
}


/**
 * xdf_writev() - adds, in a xdf file, samples coming from one or several input
 *               arrays containing the samples.
 * @xdf: pointer to a valid xdffile with mode XDF_WRITE
 * @ns: number of samples to be added
 * @vbuff: array of pointer to the array to write
 *
 * The number of arrays that must be provided on the call depends
 * on the specification of the channels.
 *
 * Return: 
 * the number of samples written in case of success, -1 otherwise
 */
API_EXPORTED int xdf_writev(struct xdf* xdf, size_t ns, void** vbuff)
{
	const char* restrict * in = (const char* restrict *)vbuff;

	if ((xdf == NULL) || !xdf->ready || (xdf->mode == XDF_READ)) {
		errno = (xdf == NULL) ? EINVAL : EPERM;
		return -1;
	}

	return writev_buffers(xdf, ns, in);
}


/**
 * xdf_read() - reads samples from a XDF file
 * @xdf: pointer to a valid xdffile with mode XDF_READ
 * @ns: number of samples to be read
 *
 * xdf_read() reads @ns samples from the xDF file referenced by
 * @xdf. This file should have been opened with mode XDF_READ and
 * xdf_prepare_transfer() should have been successfully called on it.
 * xdf_read() will fail otherwise).
 *
 * The data to be read will be transferred into arrays specified by pointers
 * provided in the variable list of arguments of the function.  The function
 * expects the same number of arrays as specified by previous call to
 * xdf_define_arrays(). The internal organisation of the data in the
 * arrays should have been specified previously with calls to
 * xdf_set_chconf().
 *
 * In addition, it is important to note that none of the arrays should overlap.
 *
 * Return: 
 * the number of the samples successfully read from the XDF file in
 * case of success. The number of samples read can be smaller than
 * the number requested in the end of the file is reached.
 * In case of error, -1 is returned and errno is set appropriately.
 *
 * Errors:
 * EINVAL
 *   @xdf is NULL
 *
 * EPERM
 *   no successful call to xdf_prepare_transfer() have been done on @xdf or
 *   it has been opened using the mode XDF_WRITE
 *
 * EINTR
 *   the call was interrupted by a signal before any data was written;
 *   see signal()
 *
 * EIO
 *   a low-level I/O error occurred while reading from the inode
 *
 * ESTALE
 *   stale file handle. This error can occur for NFS and for other file systems
 *
 *
 * Example:
 *    // Assume xdf references a xDF file opened for reading whose
 *    // channels source their data in 2 arrays of float whose strides
 *    // are the length of respectively 4 and 6 float values,
 *    // i.e. 16 and 24 bytes (in most platforms)
 *
 *    #define NS    3
 *    float array1[NS][4], array2[NS][6];
 *    unsigned int strides = {4*sizeof(float), 6*sizeof(float)};
 *    unsigned int i;
 *
 *    xdf_define_arrays(xdf, 2, strides);
 *    if (xdf_prepare_transfer(xdf))
 *         return 1;
 *
 *    for (i=0; i<45; i+=NS) {
 *         // Write the values to the file
 *         if (xdf_write(xdf, NS, array1, array2))
 *              return 1;
 *
 *	   // Use the values contained in array1 and array2
 * 	   ...
 *
 *    }
 *
 *    xdf_close(xdf);
 */
API_EXPORTED int xdf_read(struct xdf* xdf, size_t ns, ...)
{
	if ((xdf == NULL) || !xdf->ready || (xdf->mode == XDF_WRITE)) {
		errno = (xdf == NULL) ? EINVAL : EPERM;
		return -1;
	}

	unsigned int i;
	char* restrict out[xdf->narrays];
	va_list ap;

	// Initialization of the output buffers
	va_start(ap, ns);
	for (i=0; i<xdf->narrays; i++)
		out[i] = va_arg(ap, char*);
	va_end(ap);

	return readv_buffers(xdf, ns, out);
}


/**
 * xdf_readv() - reads samples in a xdf file and transfers them to one or
 *               several output arrays.
 * @xdf: pointer to a valid xdffile with mode XDF_READ
 * @ns: number of samples to be read
 * @vbuff: array of pointer to the arrays to write
 *
 * The number of arrays that must be provided on the call depends
 * on the specification of the channels.
 *
 * Return: the number of samples read in case of success, -1 otherwise
 */
API_EXPORTED int xdf_readv(struct xdf* xdf, size_t ns, void** vbuff)
{
	char* restrict * out = (char* restrict *)vbuff;

	if ((xdf == NULL) || !xdf->ready || (xdf->mode == XDF_WRITE)) {
		errno = (xdf == NULL) ? EINVAL : EPERM;
		return -1;
	}

	return readv_buffers(xdf, ns, out);
}


/**
 * xdf_seek() - moves the sample pointer of a xDF file
 * @xdf: pointer to a valid xdffile with mode XDF_READ
 * @offset: offset where the current sample pointer must move
 * @whence: reference of the offset
 *
 * xdf_seek() repositions the current sample pointer according to the
 * couple (@offset, @whence) where @whence can be: SEEK_SET the offset is
 * set to @offset bytes, SEEK_CUR the offset is set to its current
 * location plus @offset bytes, SEEK_END the offset is set to the size of
 * the file plus @offset bytes.
 *
 * The file referenced by @xdf should have been opened with mode
 * XDF_READ and xdf_prepare_transfer() should have been successfully
 * called on it.
 *
 * Return: 
 * the resulting offset location as measured in number of samples
 * from the beginning of the recording, in case of success.
 * Otherwise, a value of -1 is returned and errno is set to
 * indicate the error.
 *
 * Errors:
 * EINVAL
 *   @xdf is NULL or @whence is none of the allowed values
 *
 * EPERM
 *   no successful call to xdf_prepare_transfer() have been done on @xdf or
 *   it has been opened using the mode XDF_WRITE
 *
 * ERANGE
 *   the requested offset is out of the range of the recording
 *
 * EINTR
 *   the call was interrupted by a signal before any data was read;
 *   see signal()
 *
 * EIO
 *   a low-level I/O error occurred while reading from the inode
 *
 * ESTALE
 *   stale file handle. This error can occur for NFS and for other file systems
 */
API_EXPORTED int xdf_seek(struct xdf* xdf, int offset, int whence)
{
	long curpoint, reqpoint, fileoff;
	int irec, errnum = 0;
	unsigned int nsprec = xdf->ns_per_rec;

	if (!xdf || (xdf->mode != XDF_READ) || (!xdf->ready)) {
		errno = xdf ? EPERM : EINVAL;
		return -1;
	}

	curpoint = (xdf->nrecread < 0 ? 0 : xdf->nrecread)*nsprec
			- xdf->ns_buff;
	if (whence == SEEK_CUR)
		reqpoint = curpoint + offset;
	else if (whence == SEEK_SET)
		reqpoint = offset;
	else if (whence == SEEK_END)
		reqpoint = xdf->nrecord * nsprec + offset;
	else
		return xdf_set_error(EINVAL);
	
	if (reqpoint < 0 || (reqpoint >= (int)(xdf->nrecord * nsprec)))
		return xdf_set_error(ERANGE);
	
	irec = reqpoint / nsprec;
	if (irec != xdf->nrecread) {
		if (irec != xdf->nrecread + 1) {

 			mm_thr_mutex_lock(&(xdf->mtx));
			// Wait for the previous operation to be finished
			while (xdf->order && !xdf->reportval)
				mm_thr_cond_wait(&(xdf->cond), &(xdf->mtx));
			
			// Ignore pending end of file report
			if (xdf->reportval == 1)
				xdf->reportval = 0;

			fileoff = irec*xdf->filerec_size + xdf->hdr_offset;
			if ( (mm_seek(xdf->fd, fileoff, SEEK_SET) < 0)
			     || (read_diskrec(xdf)) )
				errnum = errno;

 			mm_thr_mutex_unlock(&(xdf->mtx));
			
			if (errnum)
				return xdf_set_error(errnum);

		}

		if (disk_transfer(xdf))
			return -1;
		
		xdf->nrecread = irec;
	}
	xdf->ns_buff = nsprec - reqpoint%nsprec;

	return reqpoint;
}

static const char xdffileio_string[] = PACKAGE_STRING;

/**
 * xdf_get_string() - gets a string describing the library
 *
 * Return: the string describing the library with its version number
 */
API_EXPORTED const char* xdf_get_string(void)
{
	return xdffileio_string;
}
