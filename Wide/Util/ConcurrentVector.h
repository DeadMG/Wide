#pragma once

#ifdef _MSC_VER
#if _MSC_VER >= 1600
#include "ConcurrentDetail/pplvector.h"
#else
#error "Visual Studio must be at least v10 to compile Wide."
#endif
#else
#include "ConcurrentDetail/stdvector.h"
#endif