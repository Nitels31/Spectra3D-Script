// Spectra3D Script - colored terminal output (implementation).
#include "internal/term.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

void term_init(void) {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_ERROR_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hIn, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hIn, mode);
        }
    }
#endif
}

int term_is_tty(FILE* stream) {
#ifdef _WIN32
    return _isatty(_fileno(stream)) != 0;
#else
    return isatty(fileno(stream)) != 0;
#endif
}

void term_color(FILE* stream, const char* code) {
    if (term_is_tty(stream)) {
        fputs(code, stream);
    }
}

void term_reset(FILE* stream) {
    if (term_is_tty(stream)) {
        fputs(TERM_RESET, stream);
    }
}
