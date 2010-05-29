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


/* To support those $!%@!!! systems that are not POSIX compliant
and that distinguish between text and binary files */
#ifndef O_BINARY
# ifdef _O_BINARY
#  define O_BINARY _O_BINARY
# else
#  define O_BINARY 0
# endif
#endif /* O_BINARY */

#include "xdfio.h"
#include "xdftypes.h"
#include "xdffile.h"

static const char xdffileio_string[] = PACKAGE_STRING;

#define ORDER_QUIT	2
#define ORDER_TRANSFER	1
#define ORDER_NONE	0

struct data_batch {
	unsigned int len;
	unsigned int iarray;
	unsigned int foff, moff, mskip;
};

struct convertion_data {
	struct convprm prm;
//	unsigned int stride;
	unsigned int filetypesize, memtypesize;
};


static int write_diskrec(struct xdf* xdf);
static int read_diskrec(struct xdf* xdf);
static void* transfer_thread_fn(void* ptr);

/*! \param xdf	pointer to a valid xdffile structure
 * 
 * Transpose recorde data from (channel,sample) to a (sample,channel)
 * organisation, performs any necessary conversion and write the record on
 * the file.
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
		transconv_data(xdf->ns_per_rec, dst, src, &(ch->prm), buff);

		// Write the converted data to the file. Continue writing
		// as long as not all data has been written
		reqsize = xdf->ns_per_rec * ch->filetypesize;
		fbuff = dst;
		do {
			wsize = write(xdf->fd, fbuff, reqsize);
			if (wsize == -1) 
				return -1;
			reqsize -= wsize;
			fbuff += wsize;
		} while (reqsize);
		src += ch->memtypesize;
	}

	// Make sure that the whole record has been sent to hardware
	if (fsync(xdf->fd))
		return -1;
	xdf->nrecord++;

	return 0;
}

static int read_diskrec(struct xdf* xdf)
{
	(void)xdf;
	return 0;
}


/*! \param ptr	pointer to a valid xdffile structure
 *
 * This is the function implementing the background thread transfering data
 * from/to the underlying file. 
 * This performs the transfer of the back buffer whenever the condition is
 * signaled and order is ORDER_TRANSFER. The end of the transfer is notified by raising
 * the semaphore
 */
static void* transfer_thread_fn(void* ptr)
{
	sigset_t mask;
	struct xdf* xdf = ptr;
	int ret = 0, wmode = (xdf->mode == XDF_WRITE) ? 1 : 0;

	sigemptyset(&mask);
	sigaddset(&mask, SIGXFSZ);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

	// Once the routine hold the mutex, it is in a ready state, notify
	// the main thread with the semaphore
 	pthread_mutex_lock(&(xdf->mtx));
	sem_post(&(xdf->sem));
	
 	// While a transfer is performed, this routine holds the mutex
	// preventing from any early buffer swap
	while (1) {
		// Wait for an order of transfer
		while (!xdf->order)
			pthread_cond_wait(&(xdf->cond), &(xdf->mtx));
	
		// break the transfer loop if the quit order has been sent
		if (xdf->order == ORDER_QUIT)
			break;

		// Write/Read a record
		ret = wmode ? write_diskrec(xdf) : read_diskrec(xdf);
		if (ret) 
			xdf->error = errno;

		// The transfer has been performed => clear the order
		// and notify the main thread through the semaphore
		xdf->order = 0;
		sem_post(&(xdf->sem));
	}
	pthread_mutex_unlock(&(xdf->mtx));
	return NULL;
}


/*! \param xdf	pointer to a valid xdffile structure
 *
 * Notify the background thread that a record has to be written or read,
 * depending on the mode of the xdf structure. This function will block if
 * the previous transfer is still being performed.
 */
static int disk_transfer(struct xdf* xdf)
{
	int retval = 0;
	void* buffer;

	// Wait for the previous operation to be finished
	sem_wait(&(xdf->sem));
	
	// If the mutex is hold by someone else, it means that the transfer
	// routine has still not in a ready state
	pthread_mutex_lock(&(xdf->mtx));

	if (xdf->error) {
		errno = xdf->err_signaled = xdf->error;
		retval = -1;
		sem_post(&(xdf->sem));
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

static void reset_batch(struct data_batch* batch, unsigned int iarray, unsigned int foff)
{
	memset(batch, 0, sizeof(*batch));
	batch->iarray = iarray;
	batch->foff = foff;
	batch->len = 0;
}

static int add_to_batch(struct data_batch *curr, const struct xdfch *ch, unsigned int foff)
{
	unsigned int datalen = get_data_size(ch->inmemtype);

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
	    	   && (curr->moff+curr->len == ch->offset)) {
			curr->len += datalen;
			return 1;
		}
	}
	return 0;
}

static void link_batches(struct xdf* xdf, unsigned int nbatch)
{
	unsigned int i;
	struct data_batch* batch = xdf->batch;
	unsigned int* stride = xdf->array_stride; 

	if (!nbatch)
		return;

	for (i=0; i<nbatch-1; i++) {
		if (batch[i].iarray == batch[i+1].iarray)
			batch[i].mskip = batch[i+1].moff - batch[i].moff;
		else
			batch[i].mskip = stride[batch[i].iarray] - batch[i].moff;
	}
	batch[nbatch-1].mskip = stride[batch[nbatch-1].iarray] - batch[nbatch-1].moff;
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
		
		// Scan channels in the xdffile order to find different batches
		for (ch=xdf->channels; ch; ch=ch->next) {
			dlen = get_data_size(ch->inmemtype);

			// Consistency checks
			if (ch->iarray > xdf->narrays
			    || ch->offset + dlen > xdf->array_stride[ch->iarray])
				return -1;

			// Linearize the processing of channel sourcing
			// the same input array
			if ((iarr == ch->iarray)
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
static unsigned int compute_sample_size(const struct xdf* xdf)
{
	unsigned int sample_size = 0;
	struct xdfch* ch = xdf->channels;

	for (ch=xdf->channels; ch; ch = ch->next) 
		sample_size += 	get_data_size(ch->inmemtype);
	return sample_size;
}


static int alloc_transfer_objects(struct xdf* xdf)
{
	unsigned int samsize;
	xdf->sample_size = samsize = compute_sample_size(xdf);

	if ( !(xdf->array_pos = malloc(xdf->narrays*sizeof(*(xdf->array_pos))))
  	    || !(xdf->convdata = malloc(xdf->numch*sizeof(*(xdf->convdata))))
	    || !(xdf->batch = malloc(xdf->nbatch*sizeof(*(xdf->batch))))  
	    || !(xdf->buff = malloc(xdf->ns_per_rec * samsize)) 
	    || !(xdf->backbuff = malloc(xdf->ns_per_rec * samsize)) 
	    || !(xdf->tmpbuff[0] = malloc(xdf->ns_per_rec * 8)) 
	    || !(xdf->tmpbuff[1] = malloc(xdf->ns_per_rec * 8)) ) {
		return -1;
	}
	return 0;
}


static void free_transfer_objects(struct xdf* xdf)
{
	free(xdf->array_pos);
	free(xdf->convdata);
	free(xdf->batch);
	free(xdf->buff);
	free(xdf->backbuff);
	free(xdf->tmpbuff[0]);
	free(xdf->tmpbuff[1]);
	xdf->array_pos = NULL;
	xdf->convdata = NULL;
	xdf->batch = NULL;
	xdf->buff = xdf->backbuff = xdf->tmpbuff[0] = xdf->tmpbuff[1] = NULL;
}



static void setup_convdata(struct xdf* xdf)
{
	unsigned int i, in_str, out_str;
	enum xdftype in_tp, out_tp;
	const double *in_mm, *out_mm;
	struct xdfch* ch = xdf->channels;

	for (i=0; i<xdf->numch; i++) {
		if (xdf->mode == XDF_WRITE) {
			// In write mode, convertion in 
			// from mem/physical to file/digital
			in_tp = ch->inmemtype;
			in_str = xdf->sample_size;
			in_mm = ch->physical_mm;
			out_tp = ch->infiletype;
			out_str = get_data_size(out_tp);
			out_mm = ch->digital_mm;
		} else {
			// In read mode, convertion in 
			// from file/digital to mem/physical
			in_tp = ch->infiletype;
			in_str = get_data_size(out_tp);
			in_mm = ch->digital_mm;
			out_tp = ch->inmemtype;
			out_str = xdf->sample_size;
			out_mm = ch->physical_mm;
		}
			
		// If data manipulated in memory is digital => no scaling
		if (ch->digital_inmem)
			in_mm = out_mm = NULL;
		
		xdf->convdata[i].filetypesize = get_data_size(ch->infiletype);
		xdf->convdata[i].memtypesize = get_data_size(ch->inmemtype);
		setup_transform(&(xdf->convdata[i].prm),
		                in_str, in_tp, in_mm,
		                out_str, out_tp, out_mm);
		ch = ch->next;
	}
}

static int setup_transfer_thread(struct xdf* xdf)
{
	int ret;
	int done = 0;

	xdf->order = ORDER_NONE;

	if ((ret = pthread_mutex_init(&(xdf->mtx), NULL)))
		goto error;
	done++;

	if ((ret = pthread_cond_init(&(xdf->cond), NULL)))
		goto error;
	done++;

	sem_init(&(xdf->sem), 0, 0);
	xdf->error = xdf->err_signaled = 0;
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

static int finish_record(struct xdf* xdf)
{
	char* buffer = (char*)xdf->buff + xdf->sample_size * xdf->ns_buff;
	unsigned int ns = xdf->ns_per_rec - xdf->ns_buff;
	int retval;

	if (!xdf->ns_buff)
		return 0;

	// Fill the remaining of the record with 0 values
	while (ns--) {
		memset(buffer, 0, xdf->sample_size);
		buffer += xdf->sample_size;
	}

	retval = disk_transfer(xdf);
	xdf->ns_buff = 0;
	return retval;
}


static int init_file_content(struct xdf* xdf)
{
	int retval = 0;
	sigset_t mask, oldmask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGXFSZ);
	pthread_sigmask(SIG_BLOCK, &mask, &oldmask);
	
	if (xdf->ops->write_header(xdf) || fsync(xdf->fd))
		retval = -1;
	
	pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
	return retval;
}


static int finish_xdffile(struct xdf* xdf)
{
	int retval = 0;
	sigset_t mask, oldmask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGXFSZ);
	pthread_sigmask(SIG_BLOCK, &mask, &oldmask);
	
	if (xdf->ops->close_file(xdf) || fsync(xdf->fd) || (close(xdf->fd) < 0))
		retval = -1;

	pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
	return retval;
}

int xdf_close(struct xdf* xdf)
{
	int retval = 0;
	struct xdfch *ch, *prev;

	if (!xdf)
		return set_xdf_error(EINVAL);

	if (xdf->ready) {
		if (xdf->mode == XDF_WRITE) {
			if (finish_record(xdf))
				retval = -1;
		}

		// Wait for the last transfer to be done and 
		sem_wait(&(xdf->sem));

		// Stop the transfer thread and wait for its end
		pthread_mutex_lock(&(xdf->mtx));
		xdf->order = ORDER_QUIT;
		pthread_cond_signal(&(xdf->cond));
		pthread_mutex_unlock(&(xdf->mtx));
		pthread_join(xdf->thid, NULL);

		// Destroy synchronization primitives
		pthread_mutex_destroy(&(xdf->mtx));
		pthread_cond_destroy(&(xdf->cond));

		// Free all allocated buffers
		free_transfer_objects(xdf);
	}

	// Finish and close the file
	if (finish_xdffile(xdf))
		retval = -1;

	// Free channels and file
	free(xdf->array_stride);
	ch=xdf->channels;
	while (ch) {
		prev = ch;
		ch = ch->next;
		xdf->ops->free_channel(prev);
	}
	xdf->ops->free_file(xdf);

	return retval;
}


int xdf_define_arrays(struct xdf* xdf, unsigned int numarrays, unsigned int* strides)
{
	unsigned int* newstrides;
	if (!(newstrides = malloc(numarrays*sizeof(*(xdf->array_stride))))) 
		return -1;

	free(xdf->array_stride);
	xdf->array_stride = newstrides;
	xdf->narrays = numarrays;
	memcpy(xdf->array_stride, strides, numarrays*sizeof(*(xdf->array_stride)));

	return 0;
}


/*!
 * xdf->array_pos, xdf->convdata and xdf->batch are assumed to be NULL
 */
int xdf_prepare_transfer(struct xdf* xdf)
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

	if (setup_transfer_thread(xdf))
		goto error;

	xdf->ready = 1;
	return 0;

error:
	free_transfer_objects(xdf);
	xdf->nbatch = 0;
	return -1;
}

/*! \param xdf 	pointer to a valid xdffile structure
 *  \param ns	number of samples to be added
 *  \param other pointer to the arrays holding the input samples
 *
 * Add samples coming from one or several input arrays containing the
 * samples. The number of arrays that must be provided on the call depends
 * on the specification of the channels.
 *
 * \warning Make sure the mode of the xdf is XDF_WRITE 
 */
int xdf_write(struct xdf* xdf, unsigned int ns, ...)
{
	int retval = 0;

	if (xdf == NULL)
		return set_xdf_error(EINVAL);
	if (!xdf->ready || (xdf->mode == XDF_READ))
		return set_xdf_error(EPERM);
	if (xdf->err_signaled)
		return set_xdf_error(xdf->err_signaled);

	unsigned int i, k, ia, ns_buff = xdf->ns_buff, nbatch = xdf->nbatch;
	char* buffer = (char*)xdf->buff + xdf->sample_size * xdf->ns_buff;
	const char** buff_in = xdf->array_pos;
	struct data_batch* batch = xdf->batch;
	va_list ap;

	// Initialization of the input buffers
	va_start(ap, ns);
	for (ia=0; ia<xdf->narrays; ia++)
		buff_in[ia] = va_arg(ap, const char*);
	va_end(ap);

	for (i=0; i<ns; i++) {
		// Transfer the sample to the buffer by chunk
		for (k=0; k<nbatch; k++) {
			ia = batch[k].iarray;
			memcpy(buffer+batch[k].foff, buff_in[ia], batch[k].len);
			buff_in[ia] += batch[k].mskip;
		}
		buffer += xdf->sample_size;

		// Write the content of the buffer if full
		if (++ns_buff == xdf->ns_per_rec) {
			if (disk_transfer(xdf)) {
				retval = -1;
				break;
			}
			buffer = xdf->buff;
			ns_buff = 0;
		}
	}

	xdf->ns_buff = ns_buff;
	return retval;
}


int set_xdf_error(int error)
{
	if (error) {
		errno = error;
		return -1;
	}
	return 0;
}


const char* xdf_get_string(void)
{
	return xdffileio_string;
}
