// Spectra3D Script - built-in functions
#ifndef SPS_BUILTINS_H
#define SPS_BUILTINS_H

#include "internal/value.h"

typedef struct {
    const char* name;
    NativeFn   fn;
} BuiltinDef;

extern const BuiltinDef SPS_BUILTINS[];
extern const int          SPS_BUILTIN_COUNT;

#endif /* SPS_BUILTINS_H */
