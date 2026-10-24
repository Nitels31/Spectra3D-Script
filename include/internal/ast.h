// Spectra3D Script - abstract syntax tree
#ifndef SPS_AST_H
#define SPS_AST_H

#include "internal/lexer.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ND_NUMBER,
    ND_FLOAT,
    ND_STRING,
    ND_FSTRING,
    ND_INTERP,
    ND_BOOL,
    ND_NIL,
    ND_IDENT,
    ND_LIST,
    ND_DICT,
    ND_BINARY,
    ND_UNARY,
    ND_LOGICAL,
    ND_TERNARY,
    ND_IN,
    ND_CALL,
    ND_METHOD,
    ND_FIELD,
    ND_CLASS,
    ND_SWITCH,
    ND_TRY,
    ND_IMPORT,
    ND_INDEX,
    ND_ASSIGN,
    ND_AUG_ASSIGN,
    ND_VAR_DECL,
    ND_TYPED_DECL,
    ND_DEF,
    ND_IF,
    ND_WHILE,
    ND_FOR,
    ND_BLOCK,
    ND_RETURN,
    ND_BREAK,
    ND_CONTINUE,
    ND_EXPR_STMT
} NodeType;

typedef struct Node Node;

struct Node {
    NodeType   type;
    int        line;

    Node*      left;        // left operand / condition / assignment target
    Node*      right;       // right operand / value
    Node*      then_branch; // "then" branch
    Node*      else_branch; // "else" branch

    Node**     children;    // call arguments, list elements, block body
    int        child_count;

    TokenType  op;          // operator token for binary/unary/logical nodes

    char*      name;        // variable or function name
    int        name_len;
    int        value_type;  // declared type for ND_TYPED_DECL (-1 = none)

    bool       is_mutable;  // var (true) or let (false)

    int64_t    int_val;
    double     float_val;
    char*      str_val;
    int        str_len;
    bool       bool_val;
};

const char* node_type_name(NodeType type);

#endif /* SPS_AST_H */
