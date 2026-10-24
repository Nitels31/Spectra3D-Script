// Spectra3D Script - the regular expressions
//
// A small backtracking engine written in C11 without any external library.
//
// The pattern is parsed into a tree of the nodes and then compiled into a
// bytecode. The matcher walks the bytecode with an explicit stack (there is
// no recursion in the matcher), so a long text cannot overflow the stack of
// the process and a broken pattern cannot crash the program.
//
// The supported syntax:
//
//   a, b, c        the literal characters
//   .              any character except the newline
//   [abc] [a-z]    the character classes
//   [^abc] [^a-z]  the negated character classes
//   \d \D          a digit and everything except a digit
//   \w \W          a letter, a digit, '_' and everything except them
//   \s \S          a space and everything except a space
//   \n \t \r \f \v the control characters
//   \xHH           a character by its hexadecimal code
//   \\ \. \* \+ \? \( \) \[ \] \{ \} \| \^ \$ \- \/   the escaped characters
//   ^ $            the anchors: the beginning and the end of the text
//   |              the alternation
//   ( )            the capturing groups
//   (?: )          the groups without the capture
//   * + ?          the quantifiers: zero or more, one or more, zero or one
//   {n} {n,} {n,m} the counters of the repetitions
//   *? +? ?? {n,m}?   the lazy quantifiers (the shortest match)
//   \1 .. \9       the back references to the capturing groups
//
// The engine is safe: the empty repetitions cannot loop forever (the
// pattern "(a*)*" is accepted and does not hang) and a broken pattern is
// reported with an error message instead of a crash.
#ifndef SPS_REGEX_H
#define SPS_REGEX_H

#include <stdbool.h>
#include <stddef.h>

// The maximum number of the capturing groups: the back references are \1 .. \9.
#define REGEX_MAX_GROUPS 9

// The number of the offsets of the groups: the whole match and the groups.
#define REGEX_MAX_OFFSETS (2 * (REGEX_MAX_GROUPS + 1))

// The offset of the group that did not take part in the match.
#define REGEX_NO_OFFSET ((size_t)-1)

typedef struct Regex Regex;

// Compiles a pattern. The function returns NULL when the pattern is broken
// and writes the description of the error into the buffer (the buffer may
// be NULL when the error is not needed).
Regex* regex_compile(const char* pattern, char* error, size_t error_size);

// Releases a compiled pattern.
void  regex_free(Regex* regex);

// The number of the capturing groups of the pattern (0 when there are none).
int   regex_group_count(const Regex* regex);

// Matches the whole text: the pattern has to cover the text from the first
// character to the last one. The offsets of the match are optional.
bool  regex_match(Regex* regex, const char* text, size_t length,
                  size_t* match_begin, size_t* match_end);

// Searches the first match at or after the position 'from'. The offsets of
// the match and of the groups are written into the arrays when they are not
// NULL. The array of the groups has REGEX_MAX_OFFSETS elements: the first
// two are the whole match, then the pairs of the groups. The group that did
// not take part in the match has the offset (size_t)-1.
bool  regex_search(Regex* regex, const char* text, size_t length, size_t from,
                   size_t* match_begin, size_t* match_end,
                   int* group_count, size_t* group_offsets);

// Replaces every match with the text of the replacement. The replacement may
// contain $0 (the whole match), $1 .. $9 (the groups), ${1} (the groups with
// the braces) and $$ (the dollar sign). The result is a new buffer that the
// caller releases with free(). The function returns 0 on success.
int   regex_replace(Regex* regex, const char* text, size_t length,
                    const char* replacement, char** out, size_t* out_length);

// Splits the text by the matches: the matches are not in the result and the
// empty parts are kept. The parts are the new strings that the caller
// releases with free(). The function returns 0 on success.
int   regex_split(Regex* regex, const char* text, size_t length,
                  char*** out, size_t* out_count);

#endif /* SPS_REGEX_H */
