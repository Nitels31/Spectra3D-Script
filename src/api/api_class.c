// Spectra3D Script - the classes of the host application
//
// The engine exposes its own objects to the scripts: a class of the host has
// a name, a set of methods and a constructor.

#include "api_internal.h"

#include <stdio.h>

SpsHostClass* sps_host_class_new(SpsContext* context, const char* name) {
    if (!context || !name) return NULL;

    int index = vm_register_host_class(context->vm, name);
    if (index < 0) {
        sps_internal_set_error(context, "cannot create the class");
        return NULL;
    }
    context->vm->host_classes[index]->owner = context;
    return (SpsHostClass*)context->vm->host_classes[index];
}

void sps_host_class_method(SpsHostClass* klass, const char* name,
                           SpsNativeFn function, void* user) {
    if (!klass || !name || !function) return;

    HostClass* core = (HostClass*)klass;
    SpsBinding* binding = sps_internal_binding((SpsContext*)core->owner, function, user);
    if (!binding) return;

    host_class_add_method(core, name, sps_internal_trampoline, binding);
}

void sps_host_class_constructor(SpsHostClass* klass, SpsNativeFn function, void* user) {
    if (!klass || !function) return;

    HostClass* core = (HostClass*)klass;
    SpsBinding* binding = sps_internal_binding((SpsContext*)core->owner, function, user);
    if (!binding) return;

    core->constructor = sps_internal_trampoline;
    core->constructor_user = binding;
}

// The getter of a property: the object is the first argument.
static Value property_getter(VM* vm, int argc, Value* args, void* user) {
    (void)vm;
    (void)argc;
    SpsProperty* property = (SpsProperty*)user;
    if (!property || !property->getter) return value_nil();
    SpsValue result = property->getter(property->context, sps_internal_to_api(args[0]), property->user_data);
    return sps_internal_result_from_api(property->context, result);
}

// The setter of a property: the object and the new value.
static Value property_setter(VM* vm, int argc, Value* args, void* user) {
    (void)vm;
    SpsProperty* property = (SpsProperty*)user;
    if (!property || !property->setter) return value_nil();
    property->setter(property->context, sps_internal_to_api(args[0]),
                     sps_internal_to_api(args[1]), property->user_data);
    return value_nil();
}

void sps_host_class_property(SpsHostClass* klass, const char* name,
                              SpsPropertyGetter getter, SpsPropertySetter setter, void* user) {
    if (!klass || !name) return;

    HostClass* core = (HostClass*)klass;
    SpsContext* context = (SpsContext*)core->owner;

    SpsProperty* property = (SpsProperty*)malloc(sizeof(SpsProperty));
    if (!property) return;
    property->context = context;
    property->getter = getter;
    property->setter = setter;
    property->user_data = user;

    host_class_add_property(core, name, getter ? property_getter : NULL,
                            setter ? property_setter : NULL, property);
}

void sps_host_class_set_free(SpsHostClass* klass, SpsUserdataFree function) {
    if (!klass) return;
    ((HostClass*)klass)->free_fn = (void (*)(void*))function;
}

SpsValue sps_host_class_new_object(SpsHostClass* klass, void* data) {
    if (!klass) return sps_nil();

    HostClass* core = (HostClass*)klass;
    ObjUserdata* object = userdata_new(((SpsContext*)core->owner)->vm, core, data);

    SpsValue value;
    value.type = SPS_USERDATA;
    value.as.pointer = object;
    return value;
}

SpsValue sps_host_object(SpsHostClass* klass, void* data) {
    return sps_host_class_new_object(klass, data);
}

void* sps_value_to_userdata(SpsValue value) {
    if (value.type != SPS_USERDATA) return NULL;
    ObjUserdata* object = (ObjUserdata*)value.as.pointer;
    return object->data;
}

SpsHostClass* sps_value_to_host_class(SpsValue value) {
    if (value.type != SPS_USERDATA) return NULL;
    ObjUserdata* object = (ObjUserdata*)value.as.pointer;
    return (SpsHostClass*)object->klass;
}
