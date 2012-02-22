/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

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
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "xdfio.h"
#include "xdftypes.h"
#include "xdffile.h"
#include "xdfevent.h"

/***************************************************
 *                Local declarations               *
 ***************************************************/

#if HAVE_PTHREAD_SIGMASK
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
static void block_signals(sigset_t *oldmask)
{
	(void)oldmask;
}
static void unblock_signals(sigset_t *oldmask)
{
	(void)oldmask;
}
#endif /* HAVE_PTHREAD_SIGMASK */


// Orders definitions for transfer
#define ORDER_INIT	3
#define ORDER_QUIT	2
#define ORDER_TRANSFER	1
#define ORDER_NONE	0


struct data_batch {
	int len;
	int iarray;
	int foff, moff, mskip;
};

struct convertion_data {
	struct convprm prm;
	unsigned int filetypesize, memtypesize;
	int skip;
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
	char *fbuff, *src = xdf->backbuff;
	void* dst = xdf->tmpbuff[0];
	void* buff = xdf->tmpbuff[1];

	// Transfer, convert and copy to file each channel data
	for (ich = 0; ich < xdf->numch; ich++) {
		ch = xdf->convdata + ich;

		// Convert data
		xdf_transconv_data(xdf->ns_per_rec, dst, src, &(ch->prm), buff);

		// Write the converted data to the file. Continue writing
		// as long as not all data has been written
		reqsize = xdf->ns_per_rec * ch->filetypesize;
		fbuff = dst;
		do {
			wsize = write(xdf->fd, fbuff, reqsize);
			if (wsize == -1) { 
				xdf->reportval = -errno;
				return -1;
			}
			reqsize -= wsize;
			fbuff += wsize;
		} while (reqsize);
		src += ch->memtypesize;
	}

	// Make sure that the whole record has been sent to hardware
	if (fsync(xdf->fd)) {
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
	char *fbuff, *dst = xdf->backbuff;
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
			rsize = read(xdf->fd, fbuff, reqsize);
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
		xdf_transconv_data(xdf->ns_per_rec, dst,
		                   src, &(ch->prm), buff);
		dst += ch->memtypesize;
	}

	return 0;
}


/* \param ptr	pointer to a valid xdffile structure
 *
 * This is the function implementing the background thread transfering data
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
 	pthread_mutex_lock(&(xdf->mtx));
	while (1) {
		// The transfer ready to be performed
		// => clear the previous order and notify the main thread
		xdf->order = 0;
		pthread_cond_signal(&(xdf->cond));

		// Wait for an order of transfer
		while (!xdf->order)
			pthread_cond_wait(&(xdf->cond), &(xdf->mtx));
	
		// break the transfer loop if the quit order has been sent
		if (xdf->order == ORDER_QUIT)
			break;

		// Write/Read a record
		if (wmode)
			write_diskrec(xdf);
		else
			read_diskrec(xdf);

	}
	pthread_mutex_unlock(&(xdf->mtx));
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
 * In addition to usual error reporting (0 if succes, -1 if error), it
 * returns 1 if the transfer thread has reported end of file.
 */
static int disk_transfer(struct xdf* xdf)
{
	int retval = 0;
	void* buffer;

	// If the mutex is hold by someone else, it means that the transfer
	// routine has still not in a ready state
	pthread_mutex_lock(&(xdf->mtx));

	// Wait for the previous operation to be finished
	while (xdf->order && !xdf->reportval)
		pthread_cond_wait(&(xdf->cond), &(xdf->mtx));

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
		pthread_cond_signal(&(xdf->cond));
	}

	// We are safe now, the transfer can start from now
	pthread_mutex_unlock(&(xdf->mtx));

	return retval;
}


/***************************************************
 *           Batch preparation functions           *
 ***************************************************/

static void reset_batch(struct data_batch* batch, unsigned int iarray, unsigned int foff)
{
	memset(batch, 0, sizeof(*batch));
	batch->iarray = iarray;
	batch->foff = foff;
	batch->len = 0;
}


static int add_to_batch(struct data_batch *curr, const struct xdfch *ch, int foff)
{
	unsigned int datalen = xdf_get_datasize(ch->inmemtype);

	if (!curr)
		return 0;

	if ((curr->iarray == ch->iarray) || (curr->len == 0)) {
		// Check for the start of the batch
		if (curr->len == 0) {
			curr->iarray = ch->iarray;
			curr->foff = foff;
			curr->moff = ch->offset;
			curr->len = datalen;
			return 1;
		}

		// Add channel to batch
	    	if ((curr->foff+curr->len == foff)
	    	   && (curr->moff+curr->len == (int)(ch->offset))) {
			curr->len += datalen;
			return 1;
		}
	}
	return 0;
}


static void link_batches(struct xdf* xdf, unsigned int nb)
{
	unsigned int i;
	int ia;
	struct data_batch* batch = xdf->batch;
	size_t* stride = xdf->array_stride; 

	if (!nb)
		return;

	for (i=0; i<nb-1; i++) {
		ia = batch[i].iarray;
		if (ia == batch[i+1].iarray)
			batch[i].mskip = batch[i+1].moff - batch[i].moff;
		else
			batch[i].mskip = stride[ia] - batch[i].moff;
	}
	batch[nb-1].mskip = stride[batch[nb-1].iarray] - batch[nb-1].moff;
}


static int compute_batches(struct xdf* xdf, int assign)
{
	struct data_batch curr, *currb;
	unsigned int nbatch = 1, iarr, moff, foff, dlen;
	const struct xdfch* ch;

	currb = assign ? xdf->batch : &curr;
	reset_batch(currb, 0, 0);

	for (iarr=0; iarr < xdf->narrays; iarr++) {
		moff = foff = 0;
		
		// Scan channels in order to find different batches
		for (ch=xdf->channels; ch; ch=ch->next) {
			if (ch->iarray < 0)
				continue;
			dlen = xdf_get_datasize(ch->inmemtype);

			// Consistency checks
			if ((unsigned int)ch->iarray > xdf->narrays
			    || ch->offset + dlen > xdf->array_stride[ch->iarray])
				return -1;

			// Linearize the processing of channel sourcing
			// the same input array
			if ((iarr == (unsigned int)ch->iarray)
			   && !add_to_batch(currb, ch, foff)) {
				nbatch++;
				if (assign)
					currb++;
				reset_batch(currb, iarr, foff);
				add_to_batch(currb, ch, foff);
			}
			foff += dlen;
		}
	}
	if (assign)
		link_batches(xdf, nbatch);

	return nbatch;
}


// channels in the buffer are assumed to be packed
// TODO: verify this assumption
static unsigned int compute_sample_size(const struct xdf* xdf, int inmem)
{
	unsigned int sample_size = 0;
	enum xdftype type;
	struct xdfch* ch = xdf->channels;

	for (ch=xdf->channels; ch; ch = ch->next) {
		if (ch->iarray < 0)
			continue;
		type = inmem ? ch->inmemtype : ch->infiletype;
		sample_size += xdf_get_datasize(type);
	}
	return sample_size;
}


/***************************************************
 *          Transfer preparation functions         *
 ***************************************************/

/* \param xdf	pointer of a valid xdf file
 *
 * Allocate all the buffers and temporary objects needed for the transfer
 */
static int alloc_transfer_objects(struct xdf* xdf)
{
	unsigned int samsize;
	xdf->sample_size = samsize = compute_sample_size(xdf, 1);
	xdf->filerec_size = compute_sample_size(xdf, 0) * xdf->ns_per_rec;

	if ( !(xdf->convdata = malloc(xdf->numch*sizeof(*(xdf->convdata))))
	    || !(xdf->batch = malloc(xdf->nbatch*sizeof(*(xdf->batch))))  
	    || !(xdf->buff = malloc(xdf->ns_per_rec * samsize)) 
	    || !(xdf->backbuff = malloc(xdf->ns_per_rec * samsize)) 
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
}


/* \param xdf	pointer of a valid xdf file
 *
 * Setup the parameters of convertion of each channels in the xDF file.
 */
static void setup_convdata(struct xdf* xdf)
{
	unsigned int i, in_str, out_str;
	enum xdftype in_tp, out_tp;
	const double *in_mm, *out_mm;
	struct xdfch* ch = xdf->channels;
	int swaptype = 0;

	for (i=0; i<xdf->numch; i++) {
		if (xdf->mode == XDF_WRITE) {
			// In write mode, convertion in 
			// from mem/physical to file/digital
			in_tp = ch->inmemtype;
			in_str = xdf->sample_size;
			in_mm = ch->physical_mm;
			out_tp = ch->infiletype;
			out_str = xdf_get_datasize(out_tp);
			out_mm = ch->digital_mm;
			swaptype = SWAP_OUT;
		} else {
			// In read mode, convertion in 
			// from file/digital to mem/physical
			in_tp = ch->infiletype;
			in_str = xdf_get_datasize(in_tp);
			in_mm = ch->digital_mm;
			out_tp = ch->inmemtype;
			out_str = xdf->sample_size;
			out_mm = ch->physical_mm;
			swaptype = SWAP_IN;
		}
			
		// If data manipulated in memory is digital => no scaling
		if (ch->digital_inmem)
			in_mm = out_mm = NULL;
		
		xdf->convdata[i].skip = (ch->iarray < 0) ? 1 : 0;
		xdf->convdata[i].filetypesize = xdf_get_datasize(ch->infiletype);
		xdf->convdata[i].memtypesize = xdf_get_datasize(ch->inmemtype);
		xdf_setup_transform(&(xdf->convdata[i].prm), swaptype,
		                in_str, in_tp, in_mm,
		                out_str, out_tp, out_mm);
		ch = ch->next;
	}
}


/* \param xdf	pointer of a valid xdf file
 *
 * Initialize the synchronization primitives and start the transfer thread.
 */
static int init_transfer_thread(struct xdf* xdf)
{
	int ret;
	int done = 0;

	if ((ret = pthread_mutex_init(&(xdf->mtx), NULL)))
		goto error;
	done++;

	if ((ret = pthread_cond_init(&(xdf->cond), NULL)))
		goto error;
	done++;

	xdf->reportval = 0;
	xdf->order = ORDER_INIT;
	if ((ret = pthread_create(&(xdf->thid), NULL, transfer_thread_fn, xdf)))
		goto error;

	return 0;

error:
	if (done-- > 0)
		pthread_cond_destroy(&(xdf->cond));
	if (done-- > 0)
		pthread_mutex_destroy(&(xdf->mtx));
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
	pthread_mutex_lock(&(xdf->mtx));
	while (xdf->order && !xdf->reportval)
		pthread_cond_wait(&(xdf->cond), &(xdf->mtx));
	xdf->order = ORDER_QUIT;
	pthread_cond_signal(&(xdf->cond));
	pthread_mutex_unlock(&(xdf->mtx));

	// Wait for the transfer thread to complete
	pthread_join(xdf->thid, NULL);

	// Destroy synchronization primitives
	pthread_mutex_destroy(&(xdf->mtx));
	pthread_cond_destroy(&(xdf->cond));

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
	if (xdf->ops->write_header(xdf) || fsync(xdf->fd))
		retval = -1;
	
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
	if (xdf->ops->complete_file(xdf) || fsync(xdf->fd))
		retval = -1;
	unblock_signals(&oldmask);

	return retval;
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

	if ((xdf->fd >= 0) && close(xdf->fd))
		retval = -1;

	// Free channels and file
	free(xdf->array_stride);
	destroy_event_table(xdf->table);
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
	int nbatch;

	if (xdf->ready)
		return -1;

	// Just compute the number of batch (no mem allocated for them yet)
	if ( (nbatch = compute_batches(xdf, 0)) < 0 )
		goto error;
	xdf->nbatch = nbatch;

	// Alloc of temporary entities needed for convertion
	if (alloc_transfer_objects(xdf))
		goto error;

	// Setup batches, convertion parameters
	compute_batches(xdf, 1); // assign batches: memory is now allocated
	setup_convdata(xdf);

	if (xdf->mode == XDF_WRITE) {
		if (init_file_content(xdf))
			goto error;
	}

	if (init_transfer_thread(xdf))
		goto error;

	if (xdf->mode == XDF_READ) {
		disk_transfer(xdf);
		xdf->nrecread = -1;
	}

	xdf->ready = 1;
	return 0;

error:
	free_transfer_objects(xdf);
	xdf->nbatch = 0;
	return -1;
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
API_EXPORTED ssize_t xdf_write(struct xdf* xdf, size_t ns, ...)
{
	if ((xdf == NULL) || !xdf->ready || (xdf->mode == XDF_READ)) {
		errno = (xdf == NULL) ? EINVAL : EPERM;
		return -1;
	}

	unsigned int i, k, ia, nsrec = xdf->ns_per_rec;
	unsigned int nbatch = xdf->nbatch, samsize = xdf->sample_size;
	char* restrict buff = xdf->buff + samsize * xdf->ns_buff;
	struct data_batch* batch = xdf->batch;
	const char* restrict in[xdf->narrays];
	va_list ap;

	// Initialization of the input buffers
	va_start(ap, ns);
	for (ia=0; ia<xdf->narrays; ia++)
		in[ia] = va_arg(ap, const char*);
	va_end(ap);

	for (i=0; i<ns; i++) {
		// Write the content of the buffer if full
		if (xdf->ns_buff == nsrec) {
			if (disk_transfer(xdf)) 
				return (i==0) ? -1 : (ssize_t)i;
			buff = xdf->buff;
			xdf->ns_buff = 0;
		}

		// Transfer the sample to the buffer by chunk
		for (k=0; k<nbatch; k++) {
			ia = batch[k].iarray;
			memcpy(buff+batch[k].foff, in[ia], batch[k].len);
			in[ia] += batch[k].mskip;
		}
		buff += samsize;
		xdf->ns_buff++;
	}

	return (ssize_t)ns;
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
API_EXPORTED ssize_t xdf_read(struct xdf* xdf, size_t ns, ...)
{
	if ((xdf == NULL) || !xdf->ready || (xdf->mode == XDF_WRITE)) {
		errno = (xdf == NULL) ? EINVAL : EPERM;
		return -1;
	}

	unsigned int i, k, ia;
	unsigned int nbatch = xdf->nbatch, samsize = xdf->sample_size;
	char* restrict buff = xdf->buff + samsize * (xdf->ns_per_rec-xdf->ns_buff);
	struct data_batch* batch = xdf->batch;
	char* restrict out[xdf->narrays];
	va_list ap;
	int ret;

	// Initialization of the output buffers
	va_start(ap, ns);
	for (ia=0; ia<xdf->narrays; ia++)
		out[ia] = va_arg(ap, char*);
	va_end(ap);

	for (i=0; i<ns; i++) {
		// Trigger a disk read when the content of buffer is empty
		if (!xdf->ns_buff) {
			if ((ret = disk_transfer(xdf))) 
				return ((ret<0)&&(i==0)) ? -1 : (ssize_t)i;
			buff = xdf->buff;
			xdf->ns_buff = xdf->ns_per_rec;
			xdf->nrecread++;
		}

		// Transfer the sample to the buffer by chunk
		for (k=0; k < nbatch; k++) {
			ia = batch[k].iarray;
			memcpy(out[ia], buff+batch[k].foff, batch[k].len);
			out[ia] += batch[k].mskip;
		}
		buff += samsize;
		xdf->ns_buff--;
	}

	return ns;
}


/* \param xdf 		pointer to a valid xdffile with mode XDF_READ 
 * \param offset	offset where the current sample pointer must move
 * \param whence	reference of the offset
 *
 * API FUNCTION
 * Reposition the current sample pointer according to the couple 
 * (offset, whence). whence can be SEEK_SET, SEEK_CUR or SEEK_END
 * Upon successful completion, it returns the resulting offset location as
 * measured in number of samples for the begining of the recording.
 * Otherwise -1 is returned and errno is set to indicate the error
 */
API_EXPORTED off_t xdf_seek(struct xdf* xdf, off_t offset, int whence)
{
	off_t curpoint, reqpoint, fileoff;
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
	
	if (reqpoint < 0 || (reqpoint >= (off_t)(xdf->nrecord * nsprec)))
		return xdf_set_error(ERANGE);
	
	irec = reqpoint / nsprec;
	if (irec != xdf->nrecread) {
		if (irec != xdf->nrecread + 1) {

 			pthread_mutex_lock(&(xdf->mtx));
			// Wait for the previous operation to be finished
			while (xdf->order && !xdf->reportval)
				pthread_cond_wait(&(xdf->cond), &(xdf->mtx));
			
			fileoff = irec*xdf->filerec_size + xdf->hdr_offset;
			if ( (lseek(xdf->fd, fileoff, SEEK_SET) < 0)
			     || (read_diskrec(xdf)) )
				errnum = errno;
 			pthread_mutex_unlock(&(xdf->mtx));
			
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
