// Spectra3D Script - colored terminal output (ANSI escape codes).
// Colors are emitted only when writing to a terminal (TTY). When the output
// is piped to a file (tests, build logs) the stream stays clean - no escape
// codes, no broken comparisons.

#ifndef SPS_TERM_H
#define SPS_TERM_H

#include <stdio.h>

// ANSI escape codes.
#define TERM_RESET   "\033[0m"
#define TERM_BOLD    "\033[1m"
#define TERM_DIM     "\033[2m"
#define TERM_RED     "\033[31m"
#define TERM_GREEN   "\033[32m"
#define TERM_YELLOW  "\033[33m"
#define TERM_BLUE    "\033[34m"
#define TERM_MAGENTA "\033[35m"
#define TERM_CYAN    "\033[36m"
#define TERM_BRED    "\033[1;31m"   // bright red bold   -> errors
#define TERM_BYELLOW "\033[1;33m"   // bright yellow bold -> warnings
#define TERM_BGREEN  "\033[1;32m"   // bright green bold  -> ok
#define TERM_BCYAN   "\033[1;36m"   // bright cyan bold   -> info

// Enable virtual terminal processing on Windows so ANSI codes work in the
// console. Safe to call on every platform.
void term_init(void);

// Returns 1 if the stream is attached to a terminal.
int term_is_tty(FILE* stream);

// Write the color escape code to the stream if it is a terminal.
void term_color(FILE* stream, const char* code);

// Write the reset escape code to the stream if it is a terminal.
void term_reset(FILE* stream);

#endif /* SPS_TERM_H */
