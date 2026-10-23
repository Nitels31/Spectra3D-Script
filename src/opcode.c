#include "opcode.h"

const char* const OP_NAMES[] = {
    "FORMAT",
    "IMPORT",
    "TRY", "TRY_END",
    "CLASS", "GET_FIELD", "SET_FIELD", "CAST",
    "CONST", "NIL", "TRUE", "FALSE", "POP", "DUP",
    "GET_LOCAL", "GET_LOCAL_CONST", "GET_GLOBAL_CONST", "GET_LOCAL_ADD", "ADD_CONST", "SET_LOCAL", "GET_GLOBAL", "SET_GLOBAL",
    "GET_UPVAL", "SET_UPVAL", "NATIVE",
    "ADD", "SUB", "MUL", "DIV", "MOD", "NEG", "NOT",
    "IN", "EQ", "NE", "LT", "LE", "GT", "GE",
    "BITAND", "BITOR", "BITXOR", "SHL", "SHR",
    "JMP", "JMP_IF_FALSE", "JMP_IF_TRUE",
    "CALL", "METHOD", "METHOD_NAME", "ITER_INIT", "ITER_NEXT", "RETURN", "CLOSURE", "CLOSE_UPVAL", "LIST",
    "DICT", "GET_INDEX", "GET_INDEX_KEEP", "SET_INDEX", "LEN"
};

// the names must be in the same order as the opcode enum
_Static_assert(sizeof(OP_NAMES) / sizeof(OP_NAMES[0]) == OP_COUNT,
               "OP_NAMES must match the opcode enum");
