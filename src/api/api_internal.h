// Spectra3D Script - the internals of the public API
#ifndef SPS_API_INTERNAL_H
#define SPS_API_INTERNAL_H

#include "../value.h"
#include "../vm.h"
#include "../program.h"
#include "sps/sps.h"

#include <stdlib.h>
#include <string.h>

// The pool of the temporary strings.
#define SPS_SCRATCH_COUNT 8
#define SPS_SCRATCH_SIZE  512

// The binding of a native function of the host.
typedef struct {
    SpsContext* context;
    SpsNativeFn function;
    void*       user;
} SpsBinding;

// The binding of a property of a class of the host.
typedef struct {
    SpsContext*        context;
    SpsPropertyGetter  getter;
    SpsPropertySetter  setter;
    void*              user_data;
} SpsProperty;

// A class of the host: the real data lives in the core.
struct SpsHostClass {
    HostClass core;
};

// A compiled script.
struct SpsChunk {
    Program    program;
    SpsContext* context;
    char*      name;
};

// An independent instance of the virtual machine.
struct SpsContext {
    VM*            vm;
    SpsErrorHandler error_handler;
    void*          error_user;
    char           last_error[512];
    char           last_stack[1024];
    SpsDiagnostic* diagnostics;
    int            diagnostic_count;
    int            diagnostic_cap;
    SpsChunk**     chunks;
    int            chunk_count;
    int            chunk_cap;
    // a small pool of temporary strings for the conversions
    ObjString*     scratch[8];
    int            scratch_index;
};

// The conversion between the values of the core and the values of the API.
SpsValue sps_internal_to_api(Value value);
Value    sps_internal_from_api(SpsContext* context, SpsValue value);
Value    sps_internal_result_from_api(SpsContext* context, SpsValue value);

// A temporary string of the pool, the content is copied into it.
const char* sps_internal_scratch(SpsContext* context, const char* text);

// The trampoline between the core and the host, and the binding of a
// native function.
Value       sps_internal_trampoline(VM* vm, int argc, Value* args, void* user);
SpsBinding* sps_internal_binding(SpsContext* context, SpsNativeFn function, void* user);

// The error of the last operation of the context.
void sps_internal_set_error(SpsContext* context, const char* message);

#endif /* SPS_API_INTERNAL_H */
