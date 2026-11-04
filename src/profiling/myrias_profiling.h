#pragma once

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>

#define MYRIAS_PROFILE_FRAME()         FrameMark
#define MYRIAS_PROFILE_SCOPE()         ZoneScoped
#define MYRIAS_PROFILE_ZONE(name)      ZoneScopedN(name)
#define MYRIAS_PROFILE_PLOT(name, val) TracyPlot(name, (int64_t)(val))

#else

#define MYRIAS_PROFILE_FRAME()         ((void)0)
#define MYRIAS_PROFILE_SCOPE()         ((void)0)
#define MYRIAS_PROFILE_ZONE(name)      ((void)0)
#define MYRIAS_PROFILE_PLOT(name, val) ((void)0)

#endif
