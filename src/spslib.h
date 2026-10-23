// Spectra3D Script - standard library (files, time, json, math)
#ifndef SPS_STDLIB_H
#define SPS_STDLIB_H

#include "value.h"

Value sps_read_file(VM* vm, int argc, Value* args, void* user);
Value sps_write_file(VM* vm, int argc, Value* args, void* user);
Value sps_append_file(VM* vm, int argc, Value* args, void* user);
Value sps_file_exists(VM* vm, int argc, Value* args, void* user);
Value sps_delete_file(VM* vm, int argc, Value* args, void* user);
Value sps_file_size(VM* vm, int argc, Value* args, void* user);
Value sps_list_dir(VM* vm, int argc, Value* args, void* user);
Value sps_make_dir(VM* vm, int argc, Value* args, void* user);

Value sps_time(VM* vm, int argc, Value* args, void* user);
Value sps_now(VM* vm, int argc, Value* args, void* user);
Value sps_sleep(VM* vm, int argc, Value* args, void* user);

Value sps_json_parse(VM* vm, int argc, Value* args, void* user);
Value sps_json_parse_text(VM* vm, const char* text, size_t length, bool* ok);
Value sps_json_stringify(VM* vm, int argc, Value* args, void* user);
Value sps_json_pretty(VM* vm, int argc, Value* args, void* user);

// the SPL format of Spectra3D
Value spl_builtin_parse(VM* vm, int argc, Value* args, void* user);
Value spl_builtin_stringify(VM* vm, int argc, Value* args, void* user);
Value spl_builtin_pretty(VM* vm, int argc, Value* args, void* user);
Value spl_builtin_load(VM* vm, int argc, Value* args, void* user);
Value spl_builtin_save(VM* vm, int argc, Value* args, void* user);

// the collections, the formats and the assertions
Value sps_zip(VM* vm, int argc, Value* args, void* user);
Value sps_unique(VM* vm, int argc, Value* args, void* user);
Value sps_flatten(VM* vm, int argc, Value* args, void* user);
Value sps_sort_by(VM* vm, int argc, Value* args, void* user);
Value sps_merge(VM* vm, int argc, Value* args, void* user);
Value sps_deep_copy(VM* vm, int argc, Value* args, void* user);
Value sps_first(VM* vm, int argc, Value* args, void* user);
Value sps_last(VM* vm, int argc, Value* args, void* user);
Value sps_take(VM* vm, int argc, Value* args, void* user);
Value sps_skip(VM* vm, int argc, Value* args, void* user);
Value sps_index_of(VM* vm, int argc, Value* args, void* user);
Value sps_to_hex(VM* vm, int argc, Value* args, void* user);
Value sps_to_binary(VM* vm, int argc, Value* args, void* user);
Value sps_to_fixed(VM* vm, int argc, Value* args, void* user);
Value sps_pad_start(VM* vm, int argc, Value* args, void* user);
Value sps_pad_end(VM* vm, int argc, Value* args, void* user);
Value sps_char_code(VM* vm, int argc, Value* args, void* user);
Value sps_from_char_code(VM* vm, int argc, Value* args, void* user);
Value sps_assert(VM* vm, int argc, Value* args, void* user);
Value sps_assert_equal(VM* vm, int argc, Value* args, void* user);

// the regular expressions
Value sps_regex_new(VM* vm, int argc, Value* args, void* user);
Value sps_regex_match(VM* vm, int argc, Value* args, void* user);
Value sps_regex_test(VM* vm, int argc, Value* args, void* user);
Value sps_regex_search(VM* vm, int argc, Value* args, void* user);
Value sps_regex_replace(VM* vm, int argc, Value* args, void* user);
Value sps_regex_split(VM* vm, int argc, Value* args, void* user);
Value sps_regex_group_count(VM* vm, int argc, Value* args, void* user);

// the Result
Value sps_result_ok(VM* vm, int argc, Value* args, void* user);
Value sps_result_error(VM* vm, int argc, Value* args, void* user);
Value sps_is_ok(VM* vm, int argc, Value* args, void* user);
Value sps_is_error(VM* vm, int argc, Value* args, void* user);
Value sps_unwrap(VM* vm, int argc, Value* args, void* user);
Value sps_unwrap_or(VM* vm, int argc, Value* args, void* user);

// the functions of the collections
Value sps_map(VM* vm, int argc, Value* args, void* user);
Value sps_filter(VM* vm, int argc, Value* args, void* user);
Value sps_reduce(VM* vm, int argc, Value* args, void* user);

Value sps_sqrt(VM* vm, int argc, Value* args, void* user);
Value sps_sin(VM* vm, int argc, Value* args, void* user);
Value sps_cos(VM* vm, int argc, Value* args, void* user);
Value sps_tan(VM* vm, int argc, Value* args, void* user);
Value sps_atan2(VM* vm, int argc, Value* args, void* user);
Value sps_pow(VM* vm, int argc, Value* args, void* user);
Value sps_floor(VM* vm, int argc, Value* args, void* user);
Value sps_ceil(VM* vm, int argc, Value* args, void* user);
Value sps_round(VM* vm, int argc, Value* args, void* user);
Value sps_random(VM* vm, int argc, Value* args, void* user);
Value sps_random_range(VM* vm, int argc, Value* args, void* user);

// formatting: "{}", "{0}", "{name}" and the format specs like ":>10.2f"
ObjString* sps_format_value(VM* vm, Value value, const char* spec, int spec_length);
ObjString* sps_format_template(VM* vm, const char* text, int length, Value* args, int argc);
Value sps_builtin_format(VM* vm, int argc, Value* args, void* user);
Value sps_method_format(VM* vm, int argc, Value* args, void* user);

#endif /* SPS_STDLIB_H */
