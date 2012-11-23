/*
 Copyright (c) 2012, The Saffire Group
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of the <organization> nor the
       names of its contributors may be used to endorse or promote products
       derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <string.h>
#include "compiler/bytecode.h"
#include "vm/vm.h"
#include "vm/vm_opcodes.h"
#include "vm/block.h"
#include "vm/frame.h"
#include "general/dll.h"
#include "general/smm.h"
#include "objects/object.h"
#include "objects/boolean.h"
#include "objects/string.h"
#include "objects/base.h"
#include "objects/numerical.h"
#include "objects/hash.h"
#include "objects/code.h"
#include "objects/method.h"
#include "objects/null.h"
#include "interpreter/errors.h"
#include "modules/module_api.h"
#include "debug.h"

#define OBJ2STR(_obj_) smm_strdup(((t_string_object *)_obj_)->value)
#define OBJ2NUM(_obj_) (((t_numerical_object *)_obj_)->value)

#define REASON_NONE         0
#define REASON_RETURN       1
#define REASON_CONTINUE     2
#define REASON_BREAK        3
#define REASON_BREAKELSE    4



struct _object *object_userland_new(t_object *obj, va_list arg_list) {
    DEBUG_PRINT("object_create_new_instance called");

    t_object *new_obj = smm_malloc(sizeof(t_object));
    memcpy(new_obj, obj, sizeof(t_object));

    // Reset refcount for new object
    new_obj->ref_count = 0;

    // These are instances
    new_obj->flags &= ~OBJECT_TYPE_MASK;
    new_obj->flags |= OBJECT_TYPE_INSTANCE;

    return new_obj;
}

#ifdef __DEBUG
char global_buf[1024];
static char *object_user_debug(struct _object *obj) {
    sprintf(global_buf, "User object[%s]", obj->name);
    return global_buf;
}
#endif

// String object management functions
t_object_funcs userland_funcs = {
        object_userland_new,              // Allocate a new string object
        NULL,             // Free a string object
        NULL,                 // Clone a string object
#ifdef __DEBUG
        object_user_debug
#endif
};




/**
 *
 */
void saffire_vm_warning(char *str, ...) {
    va_list args;
    va_start(args, str);
    fprintf(stderr, "Warning: ");
    vfprintf(stderr, str, args);
    fprintf(stderr, "\n");
    va_end(args);
}

/**
 *
 */
void saffire_vm_error(char *str, ...) {
    va_list args;
    va_start(args, str);
    fprintf(stderr, "Error: ");
    vfprintf(stderr, str, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}


#define CALL_OP(opr, in_place, err_str) \
                obj1 = vm_frame_stack_pop(frame); \
                object_dec_ref(obj1); \
                obj2 = vm_frame_stack_pop(frame); \
                object_dec_ref(obj2); \
                \
                if (obj1->type != obj2->type) { \
                    saffire_vm_error(err_str); \
                } \
                obj3 = object_operator(obj2, opr, in_place, 1, obj1); \
                \
                object_inc_ref(obj3);   \
                vm_frame_stack_push(frame, obj3);


//#define NOT_IMPLEMENTED  printf("opcode %d is not implemented yet", opcode); exit(1); break;

// Builtin identifiers (like internal library objects etc)
//t_hash_object *builtin_identifiers = NULL;

/**
 *
 */
void vm_init(void) {
    object_init();
    builtin_identifiers = (t_hash_object *)object_new(Object_Hash);
    module_init();
}

void vm_fini(void) {
    module_fini();
    // @TODO: Implement object-destroy:  object_destroy(builtin_identifiers);
    object_fini();
}


/**
 *
 */
static t_object *_import(t_vm_frame *frame, char *module, char *class) {
    char tmp[100];
    snprintf(tmp, 99, "%s::%s", module, class);

    t_object *obj = vm_frame_get_identifier(frame, tmp);
    return obj;
}


/**
 *
 */
t_object *vm_execute(t_vm_frame *frame) {
    register t_object *obj1, *obj2, *obj3, *obj4;
    register unsigned int opcode, oparg1, oparg2;
    register char *s1;
    int reason = REASON_NONE;
    t_vm_frame *tfr;
    t_vm_frameblock *block;


    // Default return value;
    t_object *ret = Object_Null;

    for (;;) {
        // Room for some other stuff
dispatch:
        // Increase number of executions done
        frame->executions++;


#ifdef __DEBUG
        unsigned long cip = frame->ip;
#endif

        // Get opcode and additional argument
        opcode = vm_frame_get_next_opcode(frame);
        if (opcode == VM_STOP) break;
        if (opcode == VM_RESERVED) {
            saffire_vm_error("VM: Reached reserved (0xFF) opcode. Halting.\n");
        }

        // If high bit is set, get operand
        oparg1 = ((opcode & 0x80) == 0x80) ? vm_frame_get_operand(frame) : 0;
        oparg2 = ((opcode & 0xC0) == 0xC0) ? vm_frame_get_operand(frame) : 0;

#ifdef __DEBUG
        if ((opcode & 0xC0) == 0xC0) {
            DEBUG_PRINT("%08lX : 0x%02X (0x%02X, 0x%02X)\n", cip, opcode, oparg1, oparg2);
        } else if ((opcode & 0x80) == 0x80) {
            DEBUG_PRINT("%08lX : 0x%02X (0x%02X)\n", cip, opcode, oparg1);
        } else {
            DEBUG_PRINT("%08lX : 0x%02X\n", cip, opcode);
        }
#endif

        switch (opcode) {
            // Removes SP-0
            case VM_POP_TOP :
                obj1 = vm_frame_stack_pop(frame);
                object_dec_ref(obj1);
                goto dispatch;
                break;

            // Rotate / swap SP-0 and SP-1
            case VM_ROT_TWO :
                obj1 = vm_frame_stack_pop(frame);
                obj2 = vm_frame_stack_pop(frame);
                vm_frame_stack_push(frame, obj1);
                vm_frame_stack_push(frame, obj2);
                goto dispatch;
                break;

            // Rotate SP-0 to SP-2
            case VM_ROT_THREE :
                obj1 = vm_frame_stack_pop(frame);
                obj2 = vm_frame_stack_pop(frame);
                obj3 = vm_frame_stack_pop(frame);
                vm_frame_stack_push(frame, obj1);
                vm_frame_stack_push(frame, obj2);
                vm_frame_stack_push(frame, obj3);
                goto dispatch;
                break;

            // Duplicate SP-0
            case VM_DUP_TOP :
                obj1 = vm_frame_stack_fetch_top(frame);
                object_inc_ref(obj1);
                vm_frame_stack_push(frame, obj1);
                goto dispatch;
                break;

            // Rotate SP-0 to SP-3
            case VM_ROT_FOUR :
                obj1 = vm_frame_stack_pop(frame);
                obj2 = vm_frame_stack_pop(frame);
                obj3 = vm_frame_stack_pop(frame);
                obj4 = vm_frame_stack_pop(frame);
                vm_frame_stack_push(frame, obj1);
                vm_frame_stack_push(frame, obj2);
                vm_frame_stack_push(frame, obj3);
                vm_frame_stack_push(frame, obj4);
                goto dispatch;
                break;

            // No operation
            case VM_NOP :
                goto dispatch;
                break;

            // Load a global identifier
            case VM_LOAD_GLOBAL :
                obj1 = vm_frame_get_global_identifier(frame, oparg1);
                object_inc_ref(obj1);
                vm_frame_stack_push(frame, obj1);
                goto dispatch;
                break;

            // store SP+0 as a global identifier
            case VM_STORE_GLOBAL :
                obj1 = vm_frame_stack_pop(frame);
                object_dec_ref(obj1);
                s1 = vm_frame_get_name(frame, oparg1);
                vm_frame_set_global_identifier(frame, s1, obj1);
                goto dispatch;
                break;

            // Remove global identifier
            case VM_DELETE_GLOBAL :
                s1 = vm_frame_get_name(frame, oparg1);
                vm_frame_set_global_identifier(frame, s1, NULL);
                goto dispatch;
                break;

            // Load and push constant onto stack
            case VM_LOAD_CONST :
                obj1 = vm_frame_get_constant(frame, oparg1);
                object_inc_ref(obj1);
                vm_frame_stack_push(frame, obj1);
                goto dispatch;
                break;

            // Store SP+0 into identifier (either local or global)
            case VM_STORE_ID :
                obj1 = vm_frame_stack_pop(frame);
                object_dec_ref(obj1);
                s1 = vm_frame_get_name(frame, oparg1);
                DEBUG_PRINT("Storing '%s' as '%s'\n", object_debug(obj1), s1);
                vm_frame_set_identifier(frame, s1, obj1);
                goto dispatch;
                break;
                // @TODO: If string(obj1) exists in local store it there, otherwise, store in global

            // Load and push identifier onto stack (either local or global)
            case VM_LOAD_ID :
                s1 = vm_frame_get_name(frame, oparg1);
                obj1 = vm_frame_get_identifier(frame, s1);
                object_inc_ref(obj1);
                vm_frame_stack_push(frame, obj1);
                goto dispatch;
                break;

            //
            case VM_BINARY_ADD :
                CALL_OP(OPERATOR_ADD, 0, "Can only add equal types");
                goto dispatch;
                break;

            case VM_BINARY_SUB :
                CALL_OP(OPERATOR_SUB, 0, "Can only subtract equal types");
                goto dispatch;
                break;

            case VM_BINARY_MUL :
                CALL_OP(OPERATOR_MUL, 0, "Can only multiply equal types");
                goto dispatch;
                break;

            case VM_BINARY_DIV :
                CALL_OP(OPERATOR_DIV, 0, "Can only divide equal types");
                goto dispatch;
                break;

            case VM_BINARY_SHL :
                CALL_OP(OPERATOR_SHL, 0, "Can only shift equal types");
                goto dispatch;
                break;

            case VM_BINARY_SHR :
                CALL_OP(OPERATOR_SHR, 0, "Can only shift equal types");
                goto dispatch;
                break;

            case VM_BINARY_OR :
                CALL_OP(OPERATOR_OR, 0, "Can only 'binary or' equal types");
                goto dispatch;
                break;

            case VM_BINARY_AND :
                CALL_OP(OPERATOR_AND, 0, "Can only 'binary and' equal types");
                goto dispatch;
                break;

            case VM_BINARY_XOR :
                CALL_OP(OPERATOR_XOR, 0, "Can only 'binary xor' equal types")
                goto dispatch;
                break;
            case VM_INPLACE_ADD :
                CALL_OP(OPERATOR_ADD, 1, "Can only add equal types");
                goto dispatch;
                break;

            case VM_INPLACE_SUB :
                CALL_OP(OPERATOR_SUB, 1, "Can only subtract equal types");
                goto dispatch;
                break;

            case VM_INPLACE_MUL :
                CALL_OP(OPERATOR_MUL, 1, "Can only multiply equal types");
                goto dispatch;
                break;

            case VM_INPLACE_DIV :
                CALL_OP(OPERATOR_DIV, 1, "Can only divide equal types");
                goto dispatch;
                break;

            case VM_INPLACE_SHL :
                CALL_OP(OPERATOR_SHL, 1, "Can only shift equal types");
                goto dispatch;
                break;

            case VM_INPLACE_SHR :
                CALL_OP(OPERATOR_SHR, 1, "Can only shift equal types");
                goto dispatch;
                break;

            case VM_INPLACE_OR :
                CALL_OP(OPERATOR_OR, 1, "Can only 'binary or' equal types");
                goto dispatch;
                break;

            case VM_INPLACE_AND :
                CALL_OP(OPERATOR_AND, 1, "Can only 'binary and' equal types");
                goto dispatch;
                break;

            case VM_INPLACE_XOR :
                CALL_OP(OPERATOR_XOR, 1, "Can only 'binary xor' equal types")
                goto dispatch;
                break;

            // Unconditional relative jump forward
            case VM_JUMP_FORWARD :
                frame->ip += oparg1;
                goto dispatch;
                break;

            // Conditional jump on SP-0 is true
            case VM_JUMP_IF_TRUE :
                obj1 = vm_frame_stack_fetch_top(frame);
                if (! OBJECT_IS_BOOLEAN(obj1)) {
                    // Cast to boolean
                    obj2 = object_find_method(obj1, "boolean");
                    obj1 = object_call(obj1, obj2, 0);
                }

                if (IS_TRUE(obj1)) {
                    frame->ip += oparg1;
                }

                goto dispatch;
                break;

            // Conditional jump on SP-0 is false
            case VM_JUMP_IF_FALSE :
                obj1 = vm_frame_stack_fetch_top(frame);
                if (! OBJECT_IS_BOOLEAN(obj1)) {
                    // Cast to boolean
                    obj2 = object_find_method(obj1, "boolean");
                    obj1 = object_call(obj1, obj2, 0);
                }

                if (IS_FALSE(obj1)) {
                    frame->ip += oparg1;
                }
                goto dispatch;
                break;

            // Unconditional absolute jump
            case VM_JUMP_ABSOLUTE :
                frame->ip = oparg1;
                goto dispatch;
                break;

            // Duplicates the SP+0 a number of times
            case VM_DUP_TOPX :
                obj1 = vm_frame_stack_fetch_top(frame);
                for (int i=0; i!=oparg1; i++) {
                    object_inc_ref(obj1);
                    vm_frame_stack_push(frame, obj1);
                }
                goto dispatch;
                break;

            // Calls method OP+0 SP+0 from object SP+1 with OP+1 args starting from SP+2.
            case VM_CALL_METHOD :
                // This object will become "self" in our method call
                obj1 = vm_frame_stack_pop(frame);
                // Find the actual method to call inside our object (or parent classes)
                obj2 = object_find_method(obj1, (char *)vm_frame_get_constant_literal(frame, oparg1));

                // @TODO: Maybe this should just be a tuple object?

                t_method_object *method = (t_method_object *)obj2;
                t_code_object *code = (t_code_object *)method->code;
                if (code->native_func) {
                    // Create argument list
                    t_dll *args = dll_init();
                    for (int i=0; i!=oparg2; i++) {
                        obj3 = vm_frame_stack_pop(frame);
                        object_dec_ref(obj3);
                        dll_append(args, obj3);
                    }

                    obj3 = code->native_func(obj1, args);
                    dll_free(args);
                } else {
                    printf("\n\nCalling bytecode: %08lX\n\n\n", (unsigned long)code->bytecode);
                    tfr = vm_frame_new(frame, code->bytecode);

                    vm_frame_stack_debug(frame);
                    vm_frame_stack_debug(tfr);

                    // Push the arguments in the correct order onto the new stack
                    //for (int i=0; i!=oparg2; i++) {
                        // We don't need to push and pop, just copy arguments and set SP.
                        memcpy(tfr->stack + tfr->sp - oparg2, frame->stack + frame->sp, oparg2 * sizeof(t_object *));

                        frame->sp += oparg2;
                        tfr->sp -= oparg2;
                    //}

                    vm_frame_stack_debug(frame);
                    vm_frame_stack_debug(tfr);

                    obj3 = vm_execute(tfr);
                    vm_frame_destroy(tfr);
                }

                object_inc_ref(obj3);
                vm_frame_stack_push(frame, obj3);

                goto dispatch;
                break;

            // Import X as Y from Z
            case VM_IMPORT :
                // Fetch the module to import
                obj1 = vm_frame_stack_pop(frame);
                object_dec_ref(obj1);
                char *module = OBJ2STR(obj1);

                // Fetch class
                obj1 = vm_frame_stack_pop(frame);
                object_dec_ref(obj1);
                char *class = OBJ2STR(obj1);

                //printf("Importing %s from %s\n", class, module);

                obj3 = _import(frame, module, class);
                object_inc_ref(obj3);
                vm_frame_stack_push(frame, obj3);

                goto dispatch;
                break;


            case VM_SETUP_LOOP :
                vm_push_block(frame, BLOCK_TYPE_LOOP, frame->ip + oparg1, frame->sp);
                goto dispatch;
                break;
            case VM_POP_BLOCK :
                vm_pop_block(frame);
                goto dispatch;
                break;

            case VM_CONTINUE_LOOP :
                ret = object_new(Object_Numerical, oparg1);
                reason = REASON_CONTINUE;
                goto block_end;
                break;
            case VM_BREAK_LOOP :
                reason = REASON_BREAK;
                goto block_end;
                break;
            case VM_BREAKELSE_LOOP :
                reason = REASON_BREAKELSE;
                goto block_end;
                break;

            case VM_COMPARE_OP :
                obj1 = vm_frame_stack_pop(frame);
                object_dec_ref(obj1);
                obj2 = vm_frame_stack_pop(frame);
                object_dec_ref(obj2);

                if (obj1->type != obj2->type) {
                    saffire_vm_error("Cannot compare non-identical object types");
                }
                obj3 = object_comparison(obj1, oparg1, obj2);

                object_inc_ref(obj3);
                vm_frame_stack_push(frame, obj3);
                goto dispatch;
                break;

            case VM_SETUP_FINALLY :
                vm_push_block(frame, BLOCK_TYPE_FINALLY, frame->ip + oparg1, frame->sp);
                goto dispatch;
                break;
            case VM_SETUP_EXCEPT :
                vm_push_block(frame, BLOCK_TYPE_EXCEPTION, frame->ip + oparg1, frame->sp);
                goto dispatch;
                break;
            case VM_END_FINALLY :
                //
                goto dispatch;
                break;


            case VM_BUILD_CLASS :
                // pop class name
                obj1 = vm_frame_stack_pop(frame);
                object_dec_ref(obj1);

                // pop flags
                obj3 = vm_frame_stack_pop(frame);
                object_dec_ref(obj3);

                obj2 = (t_object *)smm_malloc(sizeof(t_object));
                obj2->ref_count = 0;
                obj2->type = objectTypeAny;
                obj2->name = smm_strdup(OBJ2STR(obj1));
                obj2->flags = OBJ2NUM(obj3) | OBJECT_TYPE_CLASS;
                obj2->parent = Object_Base;
                obj2->implement_count = 0;
                obj2->implements = NULL;
                obj2->methods = ht_create();
                obj2->properties = ht_create();
                obj2->constants = ht_create();
                obj2->operators = NULL;
                obj2->comparisons = NULL;
                obj2->funcs = &userland_funcs;

                // Iterate all methods
                for (int i=0; i!=oparg1; i++) {
                    // pop method name
                    obj1 = vm_frame_stack_pop(frame);
                    object_dec_ref(obj1);

                    // pop method object
                    obj3 = vm_frame_stack_pop(frame);
                    object_dec_ref(obj3);

                    printf("Adding method %s to class\n", OBJ2STR(obj1));

                    // add to class
                    ht_add(obj2->methods, OBJ2STR(obj1), obj3);
                }

                object_inc_ref(obj2);
                vm_frame_stack_push(frame, obj2);

                goto dispatch;
                break;
            case VM_MAKE_METHOD :
                // pop code object
                obj1 = vm_frame_stack_pop(frame);
                object_dec_ref(obj1);

                //obj2 = object_new(Object_Code, obj1, NULL);

                // Generate method object
                obj2 = object_new(Object_Method, 0, 0, NULL, obj1);

                // Push method object
                object_inc_ref(obj2);
                vm_frame_stack_push(frame, obj2);

                goto dispatch;
                break;
            case VM_RETURN :
                ret = vm_frame_stack_pop(frame);
                object_dec_ref(ret);

                reason = REASON_RETURN;
                goto block_end;
                break;

        } // switch(opcode) {


block_end:
        // We have reached the end of a frameblock or frame. Only use the RET object from here on.

        while (reason != REASON_NONE && frame->block_cnt > 0) {
            block = vm_fetch_block(frame);

            if (reason == REASON_CONTINUE && block->type == BLOCK_TYPE_LOOP) {
                DEBUG_PRINT("\n*** Continuing loop at %08lX\n\n", OBJ2NUM(ret));
                // Continue block
                frame->ip = OBJ2NUM(ret);
                break;
            }

            // Pop block. Not needed anymore.
            vm_pop_block(frame);

            // Unwind the stack. Make sure we are at the same level as the caller block.
            while (frame->sp < block->sp) {
                DEBUG_PRINT("Current SP: %d -> Needed SP: %d\n", frame->sp, block->sp);
                obj1 = vm_frame_stack_pop(frame);
                object_dec_ref(obj1);
            }

            if (reason == REASON_BREAK && block->type == BLOCK_TYPE_LOOP) {
                DEBUG_PRINT("\nBreaking loop to %08X\n\n", block->ip);
                frame->ip = block->ip;
                break;
            }
        }

        if (reason == REASON_RETURN) {
            // Break from the loop. We're done
            break;
        }

    } // for (;;)


    return ret;
}