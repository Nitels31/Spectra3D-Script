// Spectra3D Script - the values of the public API

#include "api_internal.h"
#include "../spl.h"

#include <stdio.h>

SpsValue sps_nil(void) {
    SpsValue value;
    value.type = SPS_NIL;
    value.as.integer = 0;
    return value;
}

SpsValue sps_bool(bool boolean) {
    SpsValue value;
    value.type = SPS_BOOL;
    value.as.boolean = boolean;
    return value;
}

SpsValue sps_int(int64_t integer) {
    SpsValue value;
    value.type = SPS_INT;
    value.as.integer = integer;
    return value;
}

SpsValue sps_float(double number) {
    SpsValue value;
    value.type = SPS_FLOAT;
    value.as.number = number;
    return value;
}

SpsValue sps_string(const char* text) {
    SpsValue value;
    value.type = SPS_STRING;
    value.as.string = text ? text : "";
    return value;
}

SpsType sps_value_type(SpsValue value) {
    return value.type;
}

bool sps_value_is_nil(SpsValue value) {
    return value.type == SPS_NIL;
}

bool sps_value_to_bool(SpsValue value) {
    switch (value.type) {
        case SPS_BOOL:  return value.as.boolean;
        case SPS_INT:   return value.as.integer != 0;
        case SPS_FLOAT: return value.as.number != 0.0;
        case SPS_NIL:   return false;
        default:        return true;
    }
}

int64_t sps_value_to_int(SpsValue value) {
    switch (value.type) {
        case SPS_INT:   return value.as.integer;
        case SPS_FLOAT: return (int64_t)value.as.number;
        case SPS_BOOL:  return value.as.boolean ? 1 : 0;
        default:        return 0;
    }
}

double sps_value_to_float(SpsValue value) {
    switch (value.type) {
        case SPS_INT:   return (double)value.as.integer;
        case SPS_FLOAT: return value.as.number;
        case SPS_BOOL:  return value.as.boolean ? 1.0 : 0.0;
        default:        return 0.0;
    }
}

const char* sps_value_to_string(SpsValue value) {
    return value.type == SPS_STRING ? value.as.string : "";
}

int sps_value_length(SpsValue value) {
    switch (value.type) {
        case SPS_STRING: return (int)strlen(value.as.string);
        case SPS_LIST: {
            ObjList* list = (ObjList*)value.as.pointer;
            return (int)list->count;
        }
        case SPS_DICT: {
            ObjDict* dict = (ObjDict*)value.as.pointer;
            return (int)dict->count;
        }
        default: return 0;
    }
}

const char* sps_type_name(SpsType type) {
    switch (type) {
        case SPS_NIL:      return "nil";
        case SPS_BOOL:     return "bool";
        case SPS_INT:      return "int";
        case SPS_FLOAT:    return "float";
        case SPS_STRING:   return "str";
        case SPS_LIST:     return "list";
        case SPS_DICT:     return "dict";
        case SPS_OBJECT:   return "object";
        case SPS_CLASS:    return "class";
        case SPS_USERDATA: return "object";
        case SPS_FUNCTION: return "fn";
    }
    return "?";
}

const char* sps_value_to_text(SpsContext* context, SpsValue value) {
    Value internal = sps_internal_from_api(context, value);
    ObjString* text = string_from_value(context->vm, internal);
    context->last_error[0] = '\0';
    return text->chars;
}

SpsValue sps_value_copy(SpsContext* context, SpsValue value) {
    if (value.type != SPS_STRING) return value;

    size_t length = strlen(value.as.string);
    char* copy = (char*)malloc(length + 1);
    if (!copy) {
        sps_internal_set_error(context, "out of memory");
        return sps_nil();
    }
    memcpy(copy, value.as.string, length + 1);

    SpsValue result = value;
    result.as.string = copy;
    return result;
}

void sps_value_free(SpsContext* context, SpsValue value) {
    (void)context;
    if (value.type == SPS_STRING) {
        free((void*)value.as.string);
    }
}

SpsValue sps_value_list(SpsContext* context, int count, const SpsValue* values) {
    if (!context) return sps_nil();

    ObjList* list = list_new(context->vm);
    for (int i = 0; i < count; i++) {
        Value item = sps_internal_from_api(context, values[i]);
        list_push(context->vm, list, item);
    }

    SpsValue result;
    result.type = SPS_LIST;
    result.as.pointer = list;
    return result;
}

void sps_value_list_append(SpsContext* context, SpsValue list, SpsValue value) {
    if (!context || list.type != SPS_LIST) return;
    ObjList* object = (ObjList*)list.as.pointer;
    list_push(context->vm, object, sps_internal_from_api(context, value));
}

// ------------------------------------------------------------------
// the SPL format
// ------------------------------------------------------------------

SpsValue sps_spl_parse(SpsContext* context, const char* text) {
    if (!context || !text) return sps_nil();

    bool ok = false;
    Value value = spl_parse(context->vm, text, strlen(text), &ok);
    if (!ok) {
        sps_internal_set_error(context, "the text is not a valid SPL document");
        return sps_nil();
    }
    return sps_internal_to_api(value);
}

SpsValue sps_spl_stringify(SpsContext* context, SpsValue value) {
    if (!context) return sps_nil();
    ObjString* text = spl_stringify(context->vm, sps_internal_from_api(context, value), false, 0);
    return sps_internal_to_api(value_obj(&text->obj));
}

SpsValue sps_spl_pretty(SpsContext* context, SpsValue value) {
    if (!context) return sps_nil();
    ObjString* text = spl_stringify(context->vm, sps_internal_from_api(context, value), true, 2);
    return sps_internal_to_api(value_obj(&text->obj));
}

bool sps_value_equal(SpsValue left, SpsValue right) {    if (left.type != right.type) {
        if (left.type == SPS_INT && right.type == SPS_FLOAT) {
            return (double)left.as.integer == right.as.number;
        }
        if (left.type == SPS_FLOAT && right.type == SPS_INT) {
            return left.as.number == (double)right.as.integer;
        }
        return false;
    }
    switch (left.type) {
        case SPS_NIL:    return true;
        case SPS_BOOL:   return left.as.boolean == right.as.boolean;
        case SPS_INT:    return left.as.integer == right.as.integer;
        case SPS_FLOAT:  return left.as.number == right.as.number;
        case SPS_STRING: return strcmp(left.as.string, right.as.string) == 0;
        default:         return left.as.pointer == right.as.pointer;
    }
}
