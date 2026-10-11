#pragma once

// Tracy profiler hooks (https://github.com/wolfpld/tracy).
// Configure with -DYATAIDON_PROFILER=ON and connect the Tracy profiler GUI to the running game;
// without the option every macro below compiles to nothing.
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#define PROFILE_SCOPE()                ZoneScoped
#define PROFILE_SCOPE_N(name)          ZoneScopedN(name)
// Attach a runtime string to the zone opened just above in the same scope. NAME also sets the
// text so "Find zone" / tracy-csvexport can group by it (they only see the text).
#define PROFILE_ZONE_TEXT(str)         do { const auto& _pz_s = (str); ZoneText(_pz_s.data(), _pz_s.size()); } while (0)
#define PROFILE_ZONE_NAME(str)         do { const auto& _pz_s = (str); ZoneName(_pz_s.data(), _pz_s.size()); ZoneText(_pz_s.data(), _pz_s.size()); } while (0)
#define PROFILE_FRAME()                FrameMark
#define PROFILE_THREAD_NAME(name)      tracy::SetThreadName(name)
#else
#define PROFILE_SCOPE()                ((void)0)
#define PROFILE_SCOPE_N(name)          ((void)0)
#define PROFILE_ZONE_TEXT(str)         ((void)0)
#define PROFILE_ZONE_NAME(str)         ((void)0)
#define PROFILE_FRAME()                ((void)0)
#define PROFILE_THREAD_NAME(name)      ((void)0)
#endif
