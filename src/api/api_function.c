// Spectra3D Script - the native functions of the public API

#include "api_internal.h"
#include "internal/builtins.h"

#include <stdio.h>

// The trampoline between the core and the host: the arguments are converted
// and the result is converted back.
Value sps_internal_trampoline(VM* vm, int argc, Value* args, void* user) {
    (void)vm;
    SpsBinding* binding = (SpsBinding*)user;
    if (!binding || !binding->function) return value_nil();

    SpsValue* converted = NULL;
    if (argc > 0) {
        converted = (SpsValue*)malloc(sizeof(SpsValue) * (size_t)argc);
        if (!converted) return value_nil();
        for (int i = 0; i < argc; i++) {
            converted[i] = sps_internal_to_api(args[i]);
        }
    }

    SpsValue result = binding->function(binding->context, argc, converted, binding->user);
    free(converted);

    return sps_internal_result_from_api(binding->context, result);
}

// Creates the binding of a native function of the host.
SpsBinding* sps_internal_binding(SpsContext* context, SpsNativeFn function, void* user) {
    SpsBinding* binding = (SpsBinding*)malloc(sizeof(SpsBinding));
    if (!binding) return NULL;
    binding->context = context;
    binding->function = function;
    binding->user = user;
    return binding;
}

void sps_register_function(SpsContext* context, const char* name,
                            SpsNativeFn function, void* user) {
    if (!context || !name || !function) return;

    SpsBinding* binding = sps_internal_binding(context, function, user);
    if (!binding) {
        sps_internal_set_error(context, "out of memory");
        return;
    }
    vm_register_native(context->vm, name, sps_internal_trampoline, binding);
}

bool sps_unregister_function(SpsContext* context, const char* name) {
    if (!context || !name) return false;

    int index = vm_find_native(context->vm, name);
    if (index < 0) return false;
    // the builtins of the language cannot be removed
    if (index < SPS_BUILTIN_COUNT) return false;

    free(context->vm->natives[index].user);
    context->vm->natives[index].fn = NULL;
    context->vm->natives[index].user = NULL;
    return true;
}

bool sps_call(SpsContext* context, SpsValue function, int argument_count,
              const SpsValue* arguments, SpsValue* result) {
    if (!context) return false;
    if (function.type != SPS_FUNCTION) {
        sps_internal_set_error(context, "the value is not a function");
        return false;
    }

    Value callee;
    callee.type = VAL_CLOSURE;
    callee.as.obj = (Obj*)function.as.pointer;

    Value* converted = NULL;
    if (argument_count > 0) {
        converted = (Value*)malloc(sizeof(Value) * (size_t)argument_count);
        if (!converted) {
            sps_internal_set_error(context, "out of memory");
            return false;
        }
        for (int i = 0; i < argument_count; i++) {
            converted[i] = sps_internal_from_api(context, arguments[i]);
        }
    }

    Value output;
    bool ok = vm_call_value(context->vm, callee, argument_count, converted, &output);
    free(converted);

    if (ok && result) *result = sps_internal_to_api(output);
    return ok;
}
