// Spectra3D Script - virtual machine
#ifndef SPS_VM_H
#define SPS_VM_H

#include "internal/program.h"

#include <setjmp.h>

#define VM_STACK_MAX         (1u << 16)  // operand stack
#define VM_FRAME_ARENA_MAX   (1u << 18)  // local slots of all active frames
#define VM_FRAME_MAX         2048
#define VM_HANDLER_MAX       256

typedef struct {
    ObjClosure* closure;
    Instr*      ip;
    Value*      slots;      // local slots inside the frame arena
    size_t      slot_base;  // offset of the slots inside the arena
    int         stack_base; // stack position where the callee was stored
    int         line;       // line of the instruction being executed
    bool        discards_result; // the frame keeps the stack as it is on return
    int         handler_base;   // the number of error handlers on entry
} CallFrame;

typedef struct {
    int    handler_offset; // the offset of the handler inside the function
    int    frame_count;    // the frame depth of the try block
    int    stack_top;
    size_t arena_top;
} ErrorHandler;

// The host can install an error handler; then the runtime reports the error
// instead of terminating the process.
typedef void (*SpsVmErrorHandler)(VM* vm, const char* message, void* user);

// A registered native function of the virtual machine.
typedef struct {
    char*        name;
    NativeFn     fn;
    void*        user;
    NativeEntry* entry;
} NativeBinding;

struct VM {
    Program*     program;
    Value*       stack;
    int          stack_top;
    Value*       frame_arena;  // arena for the local slots of every frame
    size_t       arena_top;    // used part of the arena
    CallFrame*   frames;
    int          frame_count;
    ErrorHandler* handlers;    // the active try blocks
    int          handler_count;
    int          handler_cap;
    Value*       globals;
    int          global_count;
    NativeBinding* natives;  // the registered native functions
    int          native_count;
    int          native_cap;
    HostClass**  host_classes; // the classes of the host application
    int          host_class_count;
    int          host_class_cap;
    MethodCacheEntry* method_cache; // the cache of the method lookups
    int          method_cache_size;
    Obj*         objects;
    ObjUpvalue*  open_upvalues;
    size_t       bytes_allocated;
    size_t       object_count;
    const char*  script_name;
    int          exit_code;
    jmp_buf*     error_jump;   // the buffer of the running invocation
    Instr*       error_ip;
    SpsVmErrorHandler error_handler;
    void*        error_user;
    bool         has_error;
};

VM*   vm_new(Program* program);
void  vm_free(VM* vm);
void  vm_init_globals(VM* vm);
Value vm_run(VM* vm);

// Registers a native function. The index of the function is used by the
// bytecode, so the natives have to be registered before the compilation.
int   vm_register_native(VM* vm, const char* name, NativeFn fn, void* user);
int   vm_find_native(VM* vm, const char* name);
int   vm_register_host_class(VM* vm, const char* name);

// Calls a function of the language from the outside of the virtual machine.
bool  vm_call_value(VM* vm, Value callee, int argc, Value* args, Value* out);

void  vm_runtime_error(VM* vm, int line, const char* fmt, ...);
void  vm_print_stats(VM* vm);

#endif /* SPS_VM_H */
