#ifndef COMMON_H
#define COMMON_H

/* Given an unsigned 16-bit argument X, return the value corresponding to
   X with reversed byte order.  */
#define bswap_16(x) ((((x) & 0x00FF) << 8) | \
                     (((x) & 0xFF00) >> 8))

/* Given an unsigned 32-bit argument X, return the value corresponding to
   X with reversed byte order.  */
#define bswap_32(x) ((((x) & 0x000000FF) << 24) | \
                     (((x) & 0x0000FF00) << 8) | \
                     (((x) & 0x00FF0000) >> 8) | \
                     (((x) & 0xFF000000) >> 24))

/* Given an unsigned 64-bit argument X, return the value corresponding to
   X with reversed byte order.  */
#define bswap_64(x) ((((x) & 0x00000000000000FFULL) << 56) | \
                     (((x) & 0x000000000000FF00ULL) << 40) | \
                     (((x) & 0x0000000000FF0000ULL) << 24) | \
                     (((x) & 0x00000000FF000000ULL) << 8) | \
                     (((x) & 0x000000FF00000000ULL) >> 8) | \
                     (((x) & 0x0000FF0000000000ULL) >> 24) | \
                     (((x) & 0x00FF000000000000ULL) >> 40) | \
                     (((x) & 0xFF00000000000000ULL) >> 56))

// redefine localtime_r in case the program is executed on windows
#ifdef _WIN32
	#ifndef localtime_r
	#define localtime_r(a, b) localtime_s(b, a)
	#endif /* localtime_r */
#endif /* _WIN32 */

#endif /* COMMON_H */
