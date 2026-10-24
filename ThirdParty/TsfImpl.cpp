// TinySoundFont (MIT, Bernhard Schelling) implementation lives in its own
// translation unit so none of JUCE's headers or macros touch it.
#if defined(_MSC_VER)
 #pragma warning (push, 0)
#endif

#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO          // we load through our own stream, no fopen
#include "tsf.h"

#if defined(_MSC_VER)
 #pragma warning (pop)
#endif
