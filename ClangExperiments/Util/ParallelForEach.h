#pragma once

#ifdef _MSC_VER
#if _MSC_VER >= 1600
#include "ConcurrentDetail/pplparallelforeach.h"
#else
#error "Visual Studio must be at least v10 or above to compile Wide."
#endif
#else*/
#include "ConcurrentDetail/stdparallelforeach.h"
#endif