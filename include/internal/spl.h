// Spectra3D Script - the SPL format
//
// SPL is the compact data format of Spectra3D. It is similar to JSON, but:
//
//   - the keys are not quoted:        { name = "Spectra3D" }
//   - the comments are allowed:       // and /* */
//   - the commas are optional
//   - the strings may use single quotes
//
// It is faster to parse than JSON (no key strings are allocated) and takes
// less space.
#ifndef SPS_SPL_H
#define SPS_SPL_H

#include "internal/value.h"

#include <stdbool.h>
#include <stddef.h>

// Parses a text in the SPL format. Returns nil when the text is broken.
Value spl_parse(VM* vm, const char* text, size_t length, bool* ok);

// Writes a value in the SPL format. The pretty form uses the indentation.
ObjString* spl_stringify(VM* vm, Value value, bool pretty, int indent);

#endif /* SPS_SPL_H */
