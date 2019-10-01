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
 	mmthr_mtx_lock(&(xdf->mtx));
	while (1) {
		// The transfer ready to be performed
		// => clear the previous order and notify the main thread
		xdf->order = 0;
		mmthr_cond_signal(&(xdf->cond));

		// Wait for an order of transfer
		while (!xdf->order)
			mmthr_cond_wait(&(xdf->cond), &(xdf->mtx));
	
		// break the transfer loop if the quit order has been sent
		if (xdf->order == ORDER_QUIT)
			break;

		// Write/Read a record
		if (wmode)
			write_diskrec(xdf);
		else
			read_diskrec(xdf);

	}
	mmthr_mtx_unlock(&(xdf->mtx));
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
	mmthr_mtx_lock(&(xdf->mtx));

	// Wait for the previous operation to be finished
	while (xdf->order && !xdf->reportval)
		mmthr_cond_wait(&(xdf->cond), &(xdf->mtx));

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
		mmthr_cond_signal(&(xdf->cond));
	}

	// We are safe now, the transfer can start from now
	mmthr_mtx_unlock(&(xdf->mtx));

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

	if ((ret = mmthr_mtx_init(&(xdf->mtx), 0)))
		goto error;
	done++;

	if ((ret = mmthr_cond_init(&(xdf->cond), 0)))
		goto error;
	done++;

	xdf->reportval = 0;
	xdf->order = ORDER_INIT;
	if ((ret = mmthr_create(&(xdf->thid), transfer_thread_fn, xdf)))
		goto error;

	return 0;

error:
	if (done-- > 0)
		mmthr_cond_deinit(&(xdf->cond));
	if (done-- > 0)
		mmthr_mtx_deinit(&(xdf->mtx));
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
	mmthr_mtx_lock(&(xdf->mtx));
	while (xdf->order && !xdf->reportval)
		mmthr_cond_wait(&(xdf->cond), &(xdf->mtx));
	xdf->order = ORDER_QUIT;
	mmthr_cond_signal(&(xdf->cond));
	mmthr_mtx_unlock(&(xdf->mtx));

	// Wait for the transfer thread to complete
	mmthr_join(xdf->thid, NULL);

	// Destroy synchronization primitives
	mmthr_mtx_deinit(&(xdf->mtx));
	mmthr_cond_deinit(&(xdf->cond));

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

/* \param xdf		pointer to a valid xdf file
 *
 * API FUNCTION
 * Closes the xDF file and free the resources allocated
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


/* \param xdf		pointer to a valid xdf file
 * \param numarrays	number of arrays that will be used
 * \param strides	strides for each arrays
 *
 * API FUNCTION
 * Specify the number of arrays used (used in xdf_read and xdf_write) and 
 * specify the strides for each array.
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


/* \param xdf	pointer to a valid xdf file
 *
 * API FUNCTION
 * Compute the batches, allocate the necessary data for the transfer and
 * Initialize the transfer thread
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

/* \param xdf	pointer to a valid xdf file
 *
 * API FUNCTION
 * End transfer.
 * This is the opposite of xdf_prepare_transfer(), it reset the xdf file
 * to a state where it can be reconfigured for reading.
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

/* \param xdf 	pointer to a valid xdffile with mode XDF_WRITE 
 * \param ns	number of samples to be added
 * \param other pointer to the arrays holding the input samples
 *
 * API FUNCTION
 * Add samples coming from one or several input arrays containing the
 * samples. The number of arrays that must be provided on the call depends
 * on the specification of the channels.
 * Returns the number of samples written, -1 in case of error
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


/* \param xdf 	pointer to a valid xdffile with mode XDF_WRITE
 * \param ns	number of samples to be added
 * \param vbuff array of pointer to the array to write
 *
 * API FUNCTION
 * Add samples coming from one or several input arrays containing the
 * samples. The number of arrays that must be provided on the call depends
 * on the specification of the channels.
 * Returns the number of samples written, -1 in case of error
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


/* \param xdf 	pointer to a valid xdffile with mode XDF_READ 
 * \param ns	number of samples to be read
 * \param other pointer to the arrays holding the output samples
 *
 * API FUNCTION
 * Read samples in the buffer and transfer them to one or several output
 * arrays. The number of arrays that must be provided on the call depends
 * on the specification of the channels.
 * Returns the number of samples read, -1 in case of error
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


/* \param xdf 	pointer to a valid xdffile with mode XDF_READ
 * \param ns	number of samples to be read
 * \param vbuff array of pointer to the array to write
 *
 * API FUNCTION
 * Read samples in the buffer and transfer them to one or several output
 * arrays. The number of arrays that must be provided on the call depends
 * on the specification of the channels.
 * Returns the number of samples read, -1 in case of error
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


/* \param xdf 		pointer to a valid xdffile with mode XDF_READ 
 * \param offset	offset where the current sample pointer must move
 * \param whence	reference of the offset
 *
 * API FUNCTION
 * Reposition the current sample pointer according to the couple 
 * (offset, whence). whence can be SEEK_SET, SEEK_CUR or SEEK_END
 * Upon successful completion, it returns the resulting offset location as
 * measured in number of samples for the beginning of the recording.
 * Otherwise -1 is returned and errno is set to indicate the error
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

 			mmthr_mtx_lock(&(xdf->mtx));
			// Wait for the previous operation to be finished
			while (xdf->order && !xdf->reportval)
				mmthr_cond_wait(&(xdf->cond), &(xdf->mtx));
			
			// Ignore pending end of file report
			if (xdf->reportval == 1)
				xdf->reportval = 0;

			fileoff = irec*xdf->filerec_size + xdf->hdr_offset;
			if ( (mm_seek(xdf->fd, fileoff, SEEK_SET) < 0)
			     || (read_diskrec(xdf)) )
				errnum = errno;

 			mmthr_mtx_unlock(&(xdf->mtx));
			
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

/* API FUNCTION
 * Returns the string describing the library with its version number
 */
API_EXPORTED const char* xdf_get_string(void)
{
	return xdffileio_string;
}
