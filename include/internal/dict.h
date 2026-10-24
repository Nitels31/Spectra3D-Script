// Spectra3D Script - dictionary (hash map with open addressing)
#ifndef SPS_DICT_H
#define SPS_DICT_H

#include "internal/value.h"

ObjDict* dict_new(VM* vm);
bool     dict_get(ObjDict* dict, Value key, Value* out);
void     dict_set(VM* vm, ObjDict* dict, Value key, Value value);
bool     dict_remove(ObjDict* dict, Value key);
void     dict_clear(ObjDict* dict);

#endif /* SPS_DICT_H */
