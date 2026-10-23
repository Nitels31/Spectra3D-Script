// The test checks that the C library of the language and the C++ header
// (include/spl/spl.hpp) understand the same format: both parse the files of
// the assets and write exactly the same text.

#include "spl/spl.hpp"
#include "sps/sps.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::string read_file(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return std::string();
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size < 0) size = 0;
    std::string text(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        std::size_t read = std::fread(&text[0], 1, static_cast<std::size_t>(size), file);
        text.resize(read);
    }
    std::fclose(file);
    return text;
}

int main(int argc, char** argv) {
    std::vector<std::string> files;
    for (int i = 1; i < argc; i++) {
        files.push_back(argv[i]);
    }
    if (files.empty()) {
        files.push_back("assets/level_01.spl");
    }

    SpsContext* context = sps_context_new();
    if (!context) {
        std::fprintf(stderr, "cannot create the context\n");
        return 1;
    }

    int failed = 0;
    for (const std::string& file : files) {
        const std::string text = read_file(file);
        if (text.empty()) {
            std::fprintf(stderr, "[FAIL] %s: the file is empty or missing\n", file.c_str());
            failed++;
            continue;
        }

        // --- the C++ header ---
        spl::Value cpp_value;
        spl::Error cpp_error;
        if (!spl::parse(text, cpp_value, cpp_error)) {
            std::fprintf(stderr, "[FAIL] %s: the C++ header: %s\n", file.c_str(),
                         cpp_error.to_string().c_str());
            failed++;
            continue;
        }
        const std::string cpp_text = cpp_value.dump();

        // --- the C library ---
        const SpsValue c_value = sps_spl_parse(context, text.c_str());
        if (sps_value_type(c_value) == SPS_NIL) {
            std::fprintf(stderr, "[FAIL] %s: the C library: %s\n", file.c_str(),
                         sps_context_last_error(context));
            failed++;
            continue;
        }
        const SpsValue c_text = sps_spl_stringify(context, c_value);
        const std::string c_string = sps_value_to_string(c_text);

        if (cpp_text != c_string) {
            std::fprintf(stderr, "[FAIL] %s: the results are different\n", file.c_str());
            std::fprintf(stderr, "  the C++: %s\n", cpp_text.c_str());
            std::fprintf(stderr, "  the C:   %s\n", c_string.c_str());
            failed++;
            continue;
        }

        // --- the values must be the same ---
        const spl::Value* name = cpp_value.find("name");
        std::printf("[ OK ] %s: %d characters, name = %s\n", file.c_str(),
                    static_cast<int>(cpp_text.size()),
                    (name && name->is_string()) ? name->as_string().c_str() : "-");
    }

    sps_context_free(context);

    if (failed) {
        std::fprintf(stderr, "[spl] %d file(s) failed\n", failed);
        return 1;
    }
    std::printf("[spl] the C library and the C++ header agree\n");
    return 0;
}
