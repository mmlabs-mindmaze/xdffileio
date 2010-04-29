#ifndef CHANNELS_H
#define CHANNELS_H

#include "xdfio.h"

struct xdf_channel {
	unsigned int iarray, offset;
	enum xdftype inmemtype, infiletype;
	double physical_mm[2], digital_mm[2];
	struct xdf_channel* next;
	struct xdffile* owner;
};

#endif //CHANNELS_H
