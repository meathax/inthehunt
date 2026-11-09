#include "world_crt.h"

/* C23 #embed keeps the shader inside the executable without a generated
   file. The pinned Zig compiler accepts it in C99 mode as an extension. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#endif
static const char shader_source[] = {
#embed "shaders/world_crt.hlsl"
    , 0
};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

const char *world_crt_source(void) { return shader_source; }
