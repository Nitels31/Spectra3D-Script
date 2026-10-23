// Spectra3D Script - bytecode runner (spsvm)
//
//   spsvm program.spc
#include "opcode.h"
#include "program.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    printf("Spectra3D Script virtual machine (spsvm)\n");
    printf("usage: spsvm <program.spc> [--dump]\n");
}

static void dump_program(Program* program) {
    printf("globals: %d (builtins: %d)\n", program->global_count, program->builtin_count);
    for (int i = 0; i < program->proto_count; i++) {
        FunctionProto* proto = program->protos[i];
        printf("--- function '%s' (arity=%d locals=%d upvalues=%d) ---\n",
               proto->name ? proto->name : "?", proto->arity, proto->stack_size,
               proto->upvalue_count);
        for (int j = 0; j < proto->code_len; j++) {
            Instr* instr = &proto->code[j];
            printf("  %4d  %-16s", j, OP_NAMES[instr->op]);
            switch (instr->op) {
                case OP_CONST:
                case OP_GET_LOCAL:
                case OP_SET_LOCAL:
                case OP_GET_GLOBAL:
                case OP_SET_GLOBAL:
                case OP_GET_UPVAL:
                case OP_SET_UPVAL:
                case OP_NATIVE:
                case OP_JMP:
                case OP_JMP_IF_FALSE:
                case OP_JMP_IF_TRUE:
                case OP_CLOSURE:
                case OP_CLOSE_UPVAL:
                    printf(" %d", instr->c);
                    break;
                case OP_CALL:
                case OP_LIST:
                    printf(" %d", instr->a);
                    break;
                default:
                    break;
            }
            printf("   ; line %d\n", instr->line);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    bool dump = false;
    const char* path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump") == 0) dump = true;
        else path = argv[i];
    }
    if (!path) {
        usage();
        return 1;
    }

    Program program;
    program_init(&program);

    VM* vm = vm_new(&program);

    if (!program_deserialize(&program, vm, path)) {
        vm_free(vm);
        program_release(&program);
        return 1;
    }

    vm_init_globals(vm);

    if (dump) dump_program(&program);
    else vm_run(vm);

    vm_free(vm);
    program_release(&program);
    return 0;
}
