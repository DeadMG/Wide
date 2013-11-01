#pragma once

#ifdef _MSC_VER
#if _MSC_VER == 1600
#include "ConcurrentDetail/pplsampleumap.h"
#elif _MSC_VER == 1700
#include "ConcurrentDetail/pplumap.h"
#else
#error "Visual Studio must be at least v10 or later for Wide."
#endif
#else
#include "ConcurrentDetail/stdumap.h"
#endif

