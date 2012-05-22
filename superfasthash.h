#ifndef __SUPERFASTHASH_H__
#define __SUPERFASTHASH_H__

#include <stdint.h> /* Replace with <stdint.h> if appropriate */

#undef get16bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__) \
	  || defined(_MSC_VER) || defined (__BORLANDC__) || defined (__TURBOC__)
#define get16bits(d) (*((const uint16_t *) (d)))
#endif

#if !defined (get16bits)
#define get16bits(d) ((((uint32_t)(((const uint8_t *)(d))[1])) << 8)\
					+(uint32_t)(((const uint8_t *)(d))[0]) )
#endif

uint32_t SuperFastHash (const char * data, int len);

#endif // __SUPERFASTHASH_H__
