#ifndef DEBUG_H
#define DEBUG_H

#include "assert.h"

#ifdef DEBUG
#	if __GNUC__
#		undef assert
#		define assert(c) if (!(c)) __builtin_trap()
#   elif __clang__
#		undef assert
#		define assert(c) if (!(c)) __builtin_trap()
#	elif _MSC_VER
#		undef assert
#		define assert(c) if (!(c)) __debugbreak()
#	else
#		undef assert
#		define assert(c) if (!(c)) *(volatile int *)0 = 0
#	endif
#endif

#endif
