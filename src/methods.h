// Spectra3D Script - methods of the built-in types
#ifndef SPS_METHODS_H
#define SPS_METHODS_H

#include "value.h"

typedef struct {
    const char* name;
    ValueType   type;  // the type of the receiver
    NativeFn    fn;
} MethodDef;

extern const MethodDef SPS_METHODS[];
extern const int          SPS_METHOD_COUNT;

// Builds the method groups (all methods that share a name).
void     methods_init(void);
int      method_group_count(void);
int      method_group_find(const char* name, int length);
const char* method_group_name(int group);
NativeFn method_lookup(int group, ValueType type);

#endif /* SPS_METHODS_H */
