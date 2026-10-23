#include "parser.h"
#include "value.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------
// helpers
// ------------------------------------------------------------------

static void parse_error_at(Parser* parser, int line, const char* fmt, ...) {
    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (parser->on_error) {
        parser->on_error(parser->error_user, line, message);
    } else {
        fprintf(stderr, "%s:%d:%d: error: %s\n", parser->file, line,
                parser->current.column, message);
    }
    parser->had_error = true;
    longjmp(parser->error_jump, 1);
}

static void advance(Parser* parser) {
    parser->previous = parser->current;
    parser->current = lexer_next(&parser->lexer);
}

static bool check(Parser* parser, TokenType type) {
    return parser->current.type == type;
}

static bool match(Parser* parser, TokenType type) {
    if (!check(parser, type)) return false;
    advance(parser);
    return true;
}

static void consume(Parser* parser, TokenType type, const char* message) {
    if (check(parser, type)) {
        advance(parser);
        return;
    }
    parse_error_at(parser, parser->current.line, "%s (got '%s')", message,
                   token_type_name(parser->current.type));
}

static Node* new_node(Parser* parser, NodeType type) {
    Node* node = (Node*)arena_calloc(parser->arena, 1, sizeof(Node));
    node->type = type;
    node->line = parser->previous.line;
    return node;
}

static void add_child(Parser* parser, Node* parent, Node* child) {
    Node** grown = (Node**)arena_alloc(parser->arena, sizeof(Node*) * (size_t)(parent->child_count + 1));
    if (parent->child_count > 0) {
        memcpy(grown, parent->children, sizeof(Node*) * (size_t)parent->child_count);
    }
    grown[parent->child_count] = child;
    parent->children = grown;
    parent->child_count++;
}

static char* copy_name(Parser* parser, Token token) {
    return arena_strndup(parser->arena, token.start, (size_t)token.length);
}

// The colon in front of a block is optional: "if x: { }" and "if x { }".
static void expect_colon(Parser* parser) {
    if (check(parser, TK_COLON)) {
        advance(parser);
    }
}

// The semicolon is optional: a statement ends with a line break as well.
static void expect_semicolon(Parser* parser) {
    if (match(parser, TK_SEMI)) return;
    if (check(parser, TK_RBRACE) || check(parser, TK_EOF)) return; // ';' before '}' is optional
    if (parser->current.line > parser->previous.line) return;      // the line ends the statement
    parse_error_at(parser, parser->current.line, "expected ';' after statement (got '%s')",
                   token_type_name(parser->current.type));
}

// ------------------------------------------------------------------
// top level statements
// ------------------------------------------------------------------

static Node* parse_statement(Parser* parser);
static Node* parse_expression(Parser* parser);
static Node* parse_block(Parser* parser);

static Node* parse_or(Parser* parser);
static Node* parse_and(Parser* parser);
static Node* parse_not(Parser* parser);
static Node* parse_comparison(Parser* parser);
static Node* parse_bitor(Parser* parser);
static Node* parse_bitxor(Parser* parser);
static Node* parse_bitand(Parser* parser);
static Node* parse_shift(Parser* parser);
static Node* parse_term(Parser* parser);
static Node* parse_factor(Parser* parser);
static Node* parse_unary(Parser* parser);
static Node* parse_postfix(Parser* parser);
static Node* parse_primary(Parser* parser);

static Node* parse_var_decl(Parser* parser, bool is_mutable) {
    Node* decl = new_node(parser, ND_VAR_DECL);
    decl->is_mutable = is_mutable;

    if (!check(parser, TK_IDENT)) {
        parse_error_at(parser, parser->current.line, "expected variable name");
    }
    advance(parser);
    decl->name = copy_name(parser, parser->previous);
    decl->name_len = parser->previous.length;

    if (match(parser, TK_EQ)) {
        decl->right = parse_expression(parser);
    }
    expect_semicolon(parser);
    return decl;
}

static Node* parse_def(Parser* parser) {
    Node* def = new_node(parser, ND_DEF);

    // the name is optional: "def(x) { }" is an anonymous function
    if (check(parser, TK_IDENT)) {
        advance(parser);
        def->name = copy_name(parser, parser->previous);
        def->name_len = parser->previous.length;
    }

    consume(parser, TK_LPAREN, "expected '(' after the function name");

    while (!check(parser, TK_RPAREN)) {
        if (!check(parser, TK_IDENT)) {
            parse_error_at(parser, parser->current.line, "expected parameter name");
        }
        advance(parser);
        Node* param = new_node(parser, ND_IDENT);
        param->name = copy_name(parser, parser->previous);
        param->name_len = parser->previous.length;
        add_child(parser, def, param);

        if (!match(parser, TK_COMMA)) break;
    }
    consume(parser, TK_RPAREN, "expected ')' after parameters");

    def->then_branch = parse_block(parser);
    return def;
}

static Node* parse_if(Parser* parser) {
    Node* node = new_node(parser, ND_IF);
    node->left = parse_expression(parser);
    expect_colon(parser);
    node->then_branch = parse_block(parser);

    if (check(parser, TK_ELIF)) {
        advance(parser);
        node->else_branch = parse_if(parser);
    } else if (match(parser, TK_ELSE)) {
        expect_colon(parser);
        // "else if" starts a new branch, like in C
        if (check(parser, TK_IF)) {
            advance(parser);
            node->else_branch = parse_if(parser);
        } else {
            node->else_branch = parse_block(parser);
        }
    }
    return node;
}

static Node* parse_while(Parser* parser) {
    Node* node = new_node(parser, ND_WHILE);
    node->left = parse_expression(parser);
    expect_colon(parser);
    node->then_branch = parse_block(parser);
    return node;
}

static Node* parse_for(Parser* parser) {
    Node* node = new_node(parser, ND_FOR);

    if (!check(parser, TK_IDENT)) {
        parse_error_at(parser, parser->current.line, "expected loop variable name");
    }
    advance(parser);
    node->left = new_node(parser, ND_IDENT);
    node->left->name = copy_name(parser, parser->previous);
    node->left->name_len = parser->previous.length;

    consume(parser, TK_IN, "expected 'in' after loop variable");
    node->right = parse_expression(parser);
    expect_colon(parser);
    node->then_branch = parse_block(parser);
    return node;
}

static Node* parse_return(Parser* parser) {
    Node* node = new_node(parser, ND_RETURN);
    if (!check(parser, TK_SEMI) && !check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
        node->left = parse_expression(parser);
    }
    expect_semicolon(parser);
    return node;
}

static Node* parse_block(Parser* parser) {
    Node* block = new_node(parser, ND_BLOCK);
    consume(parser, TK_LBRACE, "expected '{' to start a block");

    while (!check(parser, TK_RBRACE)) {
        if (check(parser, TK_EOF)) {
            parse_error_at(parser, parser->current.line, "unexpected end of file, expected '}'");
        }
        Node* statement = parse_statement(parser);
        add_child(parser, block, statement);
    }
    consume(parser, TK_RBRACE, "expected '}' to close the block");
    return block;
}

// Peeks the token after the current one without consuming anything.
static Token peek_token(Parser* parser) {
    Lexer saved = parser->lexer;
    Token saved_current = parser->current;
    Token next = lexer_next(&parser->lexer);
    parser->lexer = saved;
    parser->current = saved_current;
    return next;
}

// A typed declaration looks like "int x = 1;" - a type name followed by an
// identifier. Everything else ("int(42)") stays an ordinary expression.
static bool is_typed_declaration(Parser* parser) {
    if (!check(parser, TK_IDENT)) return false;
    ValueType type;
    if (!type_from_name(parser->current.start, parser->current.length, &type)) return false;
    return peek_token(parser).type == TK_IDENT;
}

static Node* parse_extends(Parser* parser, Node* node);

static Node* parse_class(Parser* parser, bool is_struct) {
    Node* node = new_node(parser, ND_CLASS);
    node->is_mutable = is_struct; // true for "struct", false for "class"

    if (!check(parser, TK_IDENT)) {
        parse_error_at(parser, parser->current.line, "expected a class name");
    }
    advance(parser);
    node->name = copy_name(parser, parser->previous);
    node->name_len = parser->previous.length;
    parse_extends(parser, node);

    consume(parser, TK_LBRACE, "expected '{' after the class name");

    Node* methods = new_node(parser, ND_BLOCK);
    while (!check(parser, TK_RBRACE)) {
        if (check(parser, TK_EOF)) {
            parse_error_at(parser, parser->current.line, "unexpected end of file in a class body");
        }
        if (check(parser, TK_DEF)) {
            advance(parser);
            add_child(parser, methods, parse_def(parser));
            continue;
        }
        Node* field = parse_statement(parser);
        if (field->type != ND_VAR_DECL && field->type != ND_TYPED_DECL) {
            parse_error_at(parser, field->line, "only fields and methods are allowed in a class body");
        }
        add_child(parser, node, field);
    }
    consume(parser, TK_RBRACE, "expected '}' after the class body");

    node->then_branch = methods;
    return node;
}

// "class Enemy extends Entity"
static Node* parse_extends(Parser* parser, Node* node) {
    if (!check(parser, TK_EXTENDS)) return node;
    advance(parser);
    if (!check(parser, TK_IDENT)) {
        parse_error_at(parser, parser->current.line, "expected the name of the parent class");
    }
    advance(parser);
    node->str_val = copy_name(parser, parser->previous);
    node->str_len = parser->previous.length;
    return node;
}

// switch value { case 1: { } case 2: { } default: { } }
static Node* parse_switch(Parser* parser) {
    Node* node = new_node(parser, ND_SWITCH);
    node->left = parse_expression(parser);
    expect_colon(parser);
    consume(parser, TK_LBRACE, "expected '{' after the switch value");

    while (!check(parser, TK_RBRACE)) {
        if (check(parser, TK_EOF)) {
            parse_error_at(parser, parser->current.line, "unexpected end of file in a switch");
        }
        Node* branch = new_node(parser, ND_BLOCK);
        if (match(parser, TK_CASE)) {
            branch->left = parse_expression(parser);
        } else if (match(parser, TK_DEFAULT)) {
            branch->left = NULL; // the default branch
        } else {
            parse_error_at(parser, parser->current.line, "expected 'case' or 'default'");
        }
        expect_colon(parser);
        branch->then_branch = parse_block(parser);
        add_child(parser, node, branch);
    }
    consume(parser, TK_RBRACE, "expected '}' after the switch");
    return node;
}

static Node* parse_statement(Parser* parser) {
    if (is_typed_declaration(parser)) {
        ValueType type;
        type_from_name(parser->current.start, parser->current.length, &type);
        advance(parser); // the type name

        Node* decl = new_node(parser, ND_TYPED_DECL);
        decl->value_type = (int)type;

        if (!check(parser, TK_IDENT)) {
            parse_error_at(parser, parser->current.line, "expected a variable name");
        }
        advance(parser);
        decl->name = copy_name(parser, parser->previous);
        decl->name_len = parser->previous.length;

        if (match(parser, TK_EQ)) {
            decl->right = parse_expression(parser);
        }
        expect_semicolon(parser);
        return decl;
    }
    if (check(parser, TK_LET)) {
        advance(parser);
        return parse_var_decl(parser, false);
    }
    if (check(parser, TK_VAR)) {
        advance(parser);
        return parse_var_decl(parser, true);
    }
    if (check(parser, TK_DEF)) {
        advance(parser);
        Node* def = parse_def(parser);
        if (!def->name) {
            parse_error_at(parser, def->line, "a function needs a name here");
        }
        return def;
    }
    if (check(parser, TK_CLASS)) {
        advance(parser);
        return parse_class(parser, false);
    }
    if (check(parser, TK_STRUCT)) {
        advance(parser);
        return parse_class(parser, true);
    }
    if (check(parser, TK_SWITCH)) {
        advance(parser);
        return parse_switch(parser);
    }
    if (check(parser, TK_IMPORT)) {
        advance(parser);
        if (!check(parser, TK_STRING)) {
            parse_error_at(parser, parser->current.line, "expected a file name after 'import'");
        }
        advance(parser);
        Node* node = new_node(parser, ND_IMPORT);
        node->str_val = copy_name(parser, parser->previous);
        node->str_len = parser->previous.length;
        if (match(parser, TK_AS)) {
            if (!check(parser, TK_IDENT)) {
                parse_error_at(parser, parser->current.line, "expected a name after 'as'");
            }
            advance(parser);
            node->name = copy_name(parser, parser->previous);
            node->name_len = parser->previous.length;
        }
        expect_semicolon(parser);
        return node;
    }
    if (check(parser, TK_TRY)) {
        advance(parser);
        Node* node = new_node(parser, ND_TRY);
        node->then_branch = parse_block(parser);
        consume(parser, TK_CATCH, "expected 'catch' after the try block");
        if (!check(parser, TK_IDENT)) {
            parse_error_at(parser, parser->current.line, "expected a variable name after 'catch'");
        }
        advance(parser);
        node->name = copy_name(parser, parser->previous);
        node->name_len = parser->previous.length;
        node->else_branch = parse_block(parser);
        return node;
    }
    if (check(parser, TK_IF)) {
        advance(parser);
        return parse_if(parser);
    }
    if (check(parser, TK_WHILE)) {
        advance(parser);
        return parse_while(parser);
    }
    if (check(parser, TK_FOR)) {
        advance(parser);
        return parse_for(parser);
    }
    if (check(parser, TK_RETURN)) {
        advance(parser);
        return parse_return(parser);
    }
    if (check(parser, TK_BREAK)) {
        advance(parser);
        Node* node = new_node(parser, ND_BREAK);
        expect_semicolon(parser);
        return node;
    }
    if (check(parser, TK_CONTINUE)) {
        advance(parser);
        Node* node = new_node(parser, ND_CONTINUE);
        expect_semicolon(parser);
        return node;
    }
    if (check(parser, TK_LBRACE)) {
        return parse_block(parser);
    }

    Node* statement = new_node(parser, ND_EXPR_STMT);
    statement->left = parse_expression(parser);
    expect_semicolon(parser);
    return statement;
}

// ------------------------------------------------------------------
// expressions
// ------------------------------------------------------------------

static Node* parse_or(Parser* parser) {
    Node* left = parse_and(parser);
    while (check(parser, TK_OR) || check(parser, TK_OR_OR)) {
        TokenType op = parser->current.type;
        advance(parser);
        Node* node = new_node(parser, ND_LOGICAL);
        node->op = op;
        node->left = left;
        node->right = parse_and(parser);
        left = node;
    }
    return left;
}

static Node* parse_and(Parser* parser) {
    Node* left = parse_not(parser);
    while (check(parser, TK_AND) || check(parser, TK_AND_AND)) {
        TokenType op = parser->current.type;
        advance(parser);
        Node* node = new_node(parser, ND_LOGICAL);
        node->op = op;
        node->left = left;
        node->right = parse_not(parser);
        left = node;
    }
    return left;
}

static Node* parse_not(Parser* parser) {
    if (check(parser, TK_NOT) || check(parser, TK_BANG)) {
        TokenType op = parser->current.type;
        advance(parser);
        Node* node = new_node(parser, ND_UNARY);
        node->op = op;
        node->right = parse_not(parser);
        return node;
    }
    return parse_comparison(parser);
}

static Node* parse_comparison(Parser* parser) {
    if (check(parser, TK_EQ)) {
        parse_error_at(parser, parser->current.line,
                       "use '==' to compare two values, '=' assigns a value");
    }
    Node* left = parse_bitor(parser);
    while (check(parser, TK_EQ_EQ) || check(parser, TK_NE) || check(parser, TK_LT) ||
           check(parser, TK_LE) || check(parser, TK_GT) || check(parser, TK_GE) ||
           check(parser, TK_IN)) {
        TokenType op = parser->current.type;
        advance(parser);
        Node* node = new_node(parser, ND_BINARY);
        node->op = op;
        node->left = left;
        node->right = parse_bitor(parser);
        left = node;
    }
    return left;
}

static Node* parse_bitor(Parser* parser) {
    Node* left = parse_bitxor(parser);
    while (check(parser, TK_PIPE)) {
        advance(parser);
        Node* node = new_node(parser, ND_BINARY);
        node->op = TK_PIPE;
        node->left = left;
        node->right = parse_bitxor(parser);
        left = node;
    }
    return left;
}

static Node* parse_bitxor(Parser* parser) {
    Node* left = parse_bitand(parser);
    while (check(parser, TK_CARET)) {
        advance(parser);
        Node* node = new_node(parser, ND_BINARY);
        node->op = TK_CARET;
        node->left = left;
        node->right = parse_bitand(parser);
        left = node;
    }
    return left;
}

static Node* parse_bitand(Parser* parser) {
    Node* left = parse_shift(parser);
    while (check(parser, TK_AMP)) {
        advance(parser);
        Node* node = new_node(parser, ND_BINARY);
        node->op = TK_AMP;
        node->left = left;
        node->right = parse_shift(parser);
        left = node;
    }
    return left;
}

static Node* parse_shift(Parser* parser) {
    Node* left = parse_term(parser);
    while (check(parser, TK_SHL) || check(parser, TK_SHR)) {
        TokenType op = parser->current.type;
        advance(parser);
        Node* node = new_node(parser, ND_BINARY);
        node->op = op;
        node->left = left;
        node->right = parse_term(parser);
        left = node;
    }
    return left;
}

static Node* parse_term(Parser* parser) {
    Node* left = parse_factor(parser);
    while (check(parser, TK_PLUS) || check(parser, TK_MINUS)) {
        TokenType op = parser->current.type;
        advance(parser);
        Node* node = new_node(parser, ND_BINARY);
        node->op = op;
        node->left = left;
        node->right = parse_factor(parser);
        left = node;
    }
    return left;
}

static Node* parse_factor(Parser* parser) {
    Node* left = parse_unary(parser);
    while (check(parser, TK_STAR) || check(parser, TK_SLASH) ||
           check(parser, TK_PERCENT)) {
        TokenType op = parser->current.type;
        advance(parser);
        Node* node = new_node(parser, ND_BINARY);
        node->op = op;
        node->left = left;
        node->right = parse_unary(parser);
        left = node;
    }
    return left;
}

static Node* parse_unary(Parser* parser) {
    if (check(parser, TK_MINUS) || check(parser, TK_PLUS)) {
        TokenType op = parser->current.type;
        advance(parser);
        Node* node = new_node(parser, ND_UNARY);
        node->op = op;
        node->right = parse_unary(parser);
        return node;
    }
    return parse_postfix(parser);
}

static Node* parse_postfix(Parser* parser) {
    Node* expr = parse_primary(parser);

    for (;;) {
        if (check(parser, TK_LPAREN)) {
            advance(parser);
            Node* call = new_node(parser, ND_CALL);
            call->left = expr;
            if (!check(parser, TK_RPAREN)) {
                do {
                    add_child(parser, call, parse_expression(parser));
                } while (match(parser, TK_COMMA));
            }
            consume(parser, TK_RPAREN, "expected ')' after arguments");
            expr = call;
            continue;
        }
        if (check(parser, TK_LBRACKET)) {
            advance(parser);
            Node* index = new_node(parser, ND_INDEX);
            index->left = expr;
            index->right = parse_expression(parser);
            consume(parser, TK_RBRACKET, "expected ']' after index");
            expr = index;
            continue;
        }
        if (check(parser, TK_DOT)) {
            advance(parser);
            if (!check(parser, TK_IDENT)) {
                parse_error_at(parser, parser->current.line, "expected a name after '.'");
            }
            advance(parser);
            char* member_name = copy_name(parser, parser->previous);
            int member_length = parser->previous.length;

            if (check(parser, TK_LPAREN)) {
                // a method call
                advance(parser);
                Node* method = new_node(parser, ND_METHOD);
                method->left = expr;
                method->name = member_name;
                method->name_len = member_length;

                if (!check(parser, TK_RPAREN)) {
                    do {
                        add_child(parser, method, parse_expression(parser));
                    } while (match(parser, TK_COMMA));
                }
                consume(parser, TK_RPAREN, "expected ')' after the method arguments");
                expr = method;
            } else {
                // a field access
                Node* field = new_node(parser, ND_FIELD);
                field->left = expr;
                field->name = member_name;
                field->name_len = member_length;
                expr = field;
            }
            continue;
        }
        break;
    }
    return expr;
}

// Parses an expression that comes from an f-string.
static Node* parse_sub_expression(Parser* parser, const char* text, int length, int line) {
    char* source = arena_strndup(parser->arena, text, (size_t)length);

    Parser sub;
    memset(&sub, 0, sizeof(sub));
    sub.arena = parser->arena;
    lexer_init(&sub.lexer, source, parser->arena);
    sub.current = lexer_next(&sub.lexer);
    sub.had_error = false;

    if (setjmp(sub.error_jump) != 0) {
        parse_error_at(parser, line, "invalid expression inside the f-string");
    }

    Node* expression = parse_expression(&sub);
    if (sub.current.type != TK_EOF) {
        parse_error_at(parser, line, "unexpected tokens inside the f-string");
    }
    return expression;
}

// Converts the doubled braces of an f-string into single ones.
static char* unescape_braces(Parser* parser, const char* text, int length) {
    char* buffer = (char*)arena_alloc(parser->arena, (size_t)length + 1);
    size_t out = 0;
    for (int i = 0; i < length; i++) {
        if (text[i] == '{' && i + 1 < length && text[i + 1] == '{') {
            buffer[out++] = '{';
            i++;
            continue;
        }
        if (text[i] == '}' && i + 1 < length && text[i + 1] == '}') {
            buffer[out++] = '}';
            i++;
            continue;
        }
        buffer[out++] = text[i];
    }
    buffer[out] = '\0';
    return buffer;
}

// Builds the parts of an f-string: the literal text and the expressions.
static Node* parse_fstring(Parser* parser, Token* token) {
    Node* node = new_node(parser, ND_FSTRING);
    const char* text = token->start;
    int length = token->length;
    int position = 0;
    int literal_start = 0;

    while (position < length) {
        char c = text[position];

        if (c == '{' && position + 1 < length && text[position + 1] == '{') {
            // "{{" is a literal "{"
            position += 2;
            continue;
        }
        if (c == '}' && position + 1 < length && text[position + 1] == '}') {
            position += 2;
            continue;
        }
        if (c != '{') {
            position++;
            continue;
        }

        // the literal part before the expression
        if (position > literal_start) {
            char* raw = lexer_unescape(parser->arena, text + literal_start, position - literal_start);
            char* literal = unescape_braces(parser, raw, (int)strlen(raw));
            Node* part = new_node(parser, ND_STRING);
            part->str_val = literal;
            part->str_len = (int)strlen(literal);
            add_child(parser, node, part);
        }

        // find the matching "}"
        int depth = 1;
        int scan = position + 1;
        int spec_start = -1;
        while (scan < length && depth > 0) {
            if (text[scan] == '{') depth++;
            else if (text[scan] == '}') {
                depth--;
                if (depth == 0) break;
            } else if (text[scan] == ':' && depth == 1 && spec_start < 0) {
                spec_start = scan;
            }
            scan++;
        }
        if (scan >= length) {
            parse_error_at(parser, token->line, "unterminated expression inside the f-string");
        }

        int expression_end = (spec_start >= 0) ? spec_start : scan;
        Node* expression = parse_sub_expression(parser, text + position + 1,
                                                 expression_end - position - 1, token->line);

        Node* part = new_node(parser, ND_INTERP);
        part->left = expression;
        if (spec_start >= 0) {
            part->str_val = arena_strndup(parser->arena, text + spec_start + 1,
                                          (size_t)(scan - spec_start - 1));
            part->str_len = scan - spec_start - 1;
        }
        add_child(parser, node, part);

        position = scan + 1;
        literal_start = position;
    }

    // the rest of the string
    if (length > literal_start) {
        char* raw = lexer_unescape(parser->arena, text + literal_start, length - literal_start);
        char* literal = unescape_braces(parser, raw, (int)strlen(raw));
        Node* part = new_node(parser, ND_STRING);
        part->str_val = literal;
        part->str_len = (int)strlen(literal);
        add_child(parser, node, part);
    }

    return node;
}

static Node* parse_primary(Parser* parser) {
    if (check(parser, TK_DEF)) {
        advance(parser);
        return parse_def(parser);
    }

    if (check(parser, TK_IDENT)) {
        advance(parser);
        Node* node = new_node(parser, ND_IDENT);
        node->name = copy_name(parser, parser->previous);
        node->name_len = parser->previous.length;
        return node;
    }

    if (check(parser, TK_INT)) {
        advance(parser);
        Node* node = new_node(parser, ND_NUMBER);
        node->int_val = parser->previous.int_val;
        return node;
    }

    if (check(parser, TK_FLOAT)) {
        advance(parser);
        Node* node = new_node(parser, ND_FLOAT);
        node->float_val = parser->previous.float_val;
        return node;
    }

    if (check(parser, TK_STRING)) {
        advance(parser);
        if (parser->previous.is_fstring) {
            return parse_fstring(parser, &parser->previous);
        }
        Node* node = new_node(parser, ND_STRING);
        node->str_val = copy_name(parser, parser->previous);
        node->str_len = parser->previous.length;
        return node;
    }

    if (check(parser, TK_TRUE) || check(parser, TK_FALSE)) {
        bool value = check(parser, TK_TRUE);
        advance(parser);
        Node* node = new_node(parser, ND_BOOL);
        node->bool_val = value;
        return node;
    }

    if (check(parser, TK_NIL)) {
        advance(parser);
        return new_node(parser, ND_NIL);
    }

    if (match(parser, TK_LPAREN)) {
        Node* expr = parse_expression(parser);
        consume(parser, TK_RPAREN, "expected ')' after expression");
        return expr;
    }

    if (match(parser, TK_LBRACKET)) {
        Node* node = new_node(parser, ND_LIST);
        if (!check(parser, TK_RBRACKET)) {
            do {
                add_child(parser, node, parse_expression(parser));
            } while (match(parser, TK_COMMA));
        }
        consume(parser, TK_RBRACKET, "expected ']' after list literal");
        return node;
    }

    if (match(parser, TK_LBRACE)) {
        Node* node = new_node(parser, ND_DICT);
        if (!check(parser, TK_RBRACE)) {
            do {
                add_child(parser, node, parse_expression(parser));
                consume(parser, TK_COLON, "expected ':' after the dictionary key");
                add_child(parser, node, parse_expression(parser));
            } while (match(parser, TK_COMMA));
        }
        consume(parser, TK_RBRACE, "expected '}' after the dictionary literal");
        return node;
    }

    parse_error_at(parser, parser->current.line, "unexpected token '%s'",
                   token_type_name(parser->current.type));
    return NULL;
}

// condition ? value_if_true : value_if_false
static Node* parse_ternary(Parser* parser) {
    Node* condition = parse_or(parser);
    if (!check(parser, TK_QUESTION)) return condition;

    advance(parser);
    Node* node = new_node(parser, ND_TERNARY);
    node->left = condition;
    node->right = parse_ternary(parser);
    consume(parser, TK_COLON, "expected ':' in the ternary expression");
    node->then_branch = parse_ternary(parser);
    return node;
}

static Node* parse_expression(Parser* parser) {
    Node* expr = parse_ternary(parser);

    if (check(parser, TK_EQ)) {
        advance(parser);
        Node* assign = new_node(parser, ND_ASSIGN);
        assign->left = expr;
        assign->right = parse_expression(parser);
        return assign;
    }

    if (check(parser, TK_PLUS_EQ) || check(parser, TK_MINUS_EQ) || check(parser, TK_STAR_EQ) ||
        check(parser, TK_SLASH_EQ) || check(parser, TK_PERCENT_EQ)) {
        TokenType op = parser->current.type;
        advance(parser);
        Node* augment = new_node(parser, ND_AUG_ASSIGN);
        switch (op) {
            case TK_PLUS_EQ:  augment->op = TK_PLUS;    break;
            case TK_MINUS_EQ: augment->op = TK_MINUS;   break;
            case TK_STAR_EQ:  augment->op = TK_STAR;    break;
            case TK_SLASH_EQ: augment->op = TK_SLASH;   break;
            default:         augment->op = TK_PERCENT; break;
        }
        augment->left = expr;
        augment->right = parse_expression(parser);
        return augment;
    }

    return expr;
}

// ------------------------------------------------------------------
// entry point
// ------------------------------------------------------------------

Node* parser_parse(Parser* parser, Arena* arena, const char* source) {
    parser->arena = arena;
    if (parser->file[0] == '\0') snprintf(parser->file, sizeof(parser->file), "<script>");
    parser->had_error = false;
    lexer_init(&parser->lexer, source, arena);

    parser->current = lexer_next(&parser->lexer);

    if (setjmp(parser->error_jump) != 0) {
        return NULL;
    }

    Node* program = new_node(parser, ND_BLOCK);
    while (!check(parser, TK_EOF)) {
        Node* statement = parse_statement(parser);
        add_child(parser, program, statement);
    }
    return program;
}
