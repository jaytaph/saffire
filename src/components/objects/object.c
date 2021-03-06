/*
 Copyright (c) 2012-2015, The Saffire Group
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of the Saffire Group the
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

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <saffire/general/string.h>
#include <saffire/objects/object.h>
#include <saffire/objects/objects.h>
#include <saffire/general/dll.h>
#include <saffire/debug.h>
#include <saffire/gc/gc.h>
#include <saffire/general/output.h>
#include <saffire/memory/smm.h>
#include <saffire/vm/thread.h>

// Include generated interfaces
#include "_generated_interfaces.inc"

// @TODO: in_place: is this option really needed? (inplace modifications of object, like A++; or A = A + 2;)
// @TODO: a++ seems like an unary operator instead?

// Object type string constants
const char *objectTypeNames[OBJECT_TYPE_LEN] = { "object", "callable", "attribute", "base", "boolean",
                                                 "null", "numerical", "regex", "string",
                                                 "hash", "tuple", "list", "exception", "user" };

// Object comparison methods. These should map on the COMPARISON_* defines
const char *objectCmpMethods[9] = { "__cmp_eq", "__cmp_ne", "__cmp_lt", "__cmp_gt", "__cmp_le", "__cmp_ge",
                                    "__cmp_in", "__cmp_ni", "__cmp_ex" };

// Object operator methods. These should map on the OPERATOR_* defines
const char *objectOprMethods[14] = { "__opr_add", "__opr_sub", "__opr_mul", "__opr_div", "__opr_mod",
                                     "__opr_and", "__opr_or", "__opr_xor", "__opr_shl", "__opr_shr",
                                     "__opr_inv", "__opr_not", "__opr_pos", "__opr_neg" };



// Forward defines
static void object_duplicate_interfaces(t_object *src_obj, t_object *dst_obj);
static void object_duplicate_attributes(t_object *src_obj, t_object *dst_obj);
static void object_instantiate(t_object *instance_obj, t_object *class_obj);

/**
 * Checks if an object is an instance of a class. Will check against parents too.

 * Returns 1 if so, 0 otherwise.
 */
int object_instance_of(t_object *obj, const char *instance) {
    DEBUG_PRINT_CHAR("object_instance_of(%s, %s)\n", obj->name, instance);

    t_object *cur_obj = obj;
    while (cur_obj != NULL) {
        DEBUG_PRINT_CHAR("  *   Checking: %s against %s\n", cur_obj->name, instance);
        // Check if name of object matches instance
        if (strcmp(cur_obj->name, instance) == 0) {
            return 1;
        }

        // @TODO: Also check interfaces

        // Trickle down to parent object and try again
        cur_obj = cur_obj->parent;
    }

    // Nothing found that matches :/
    return 0;
}


/**
 * Free an object (if needed)
 */
static void _object_free(t_object *obj) {
    if (! obj) return;

    // ref_count > 0, object is still in use somewhere else. Don't free it yet
    if (obj->ref_count > 0) return;

#ifdef __DEBUG
#if __DEBUG_FREE_OBJECT
    if (! OBJECT_IS_CALLABLE(obj) && ! OBJECT_IS_ATTRIBUTE(obj)) {
        DEBUG_PRINT_CHAR("Freeing object: %p\n", obj);
    }
#endif
#endif

    // Free attributes
    t_hash_iter iter;
    ht_iter_init(&iter, obj->attributes);
    while (ht_iter_valid(&iter)) {
        // Release attribute
        t_object *attr_obj = (t_object *)ht_iter_value(&iter);
        object_release(attr_obj);

        ht_iter_next(&iter);
    }
    ht_destroy(obj->attributes);
    obj->attributes = NULL;

    // Free interfaces
    if (obj->interfaces) {
        t_dll_element *e = DLL_HEAD(obj->interfaces);
        while (e) {
            object_release(DLL_DATA_PTR(e));
            e = DLL_NEXT(e);
        }
        dll_free(obj->interfaces);
        obj->interfaces = NULL;
    }


    // Free values from the object
    if (obj->funcs && obj->funcs->free) {
        obj->funcs->free(obj);
    }

    if (OBJECT_IS_USERLAND(obj)) {
        // Release parent class for userland created objects
        object_release(obj->parent);
        obj->parent = NULL;
    }

    if (OBJECT_IS_ALLOCATED(obj)) {
        // Free name if the object is dynamically allocated
        smm_free(obj->name);
        obj->name = NULL;
    }

    // Free the object itself
    if (obj->funcs && obj->funcs->destroy) {
        obj->funcs->destroy(obj);
    }

    // Object is destroyed. We cannot use object anymore.
    obj = NULL;


    // @TODO: here we should actually add the object to the GC destruction queue.
    // If we need the object later, we can still catch it from this queue, otherwise it will
    // be destroyed somewhere in the future.

//    if (! gc_queue_add(obj) && obj->funcs && obj->funcs->destroy) {
//        obj->funcs->destroy(obj);
//    }
}


#ifdef __DEBUG
t_hash_table *refcount_objects = NULL;
#endif

/**
 * Increases reference to an object.
 */
void object_inc_ref(t_object *obj) {
    if (! obj) return;

    obj->ref_count++;

#ifdef __DEBUG
    if (refcount_objects == NULL) {
        refcount_objects = ht_create();
    }
    ht_replace_ptr(refcount_objects, (void *)obj, (void *)(intptr_t)obj->ref_count);
#endif

    if (OBJECT_IS_CALLABLE(obj) || OBJECT_IS_ATTRIBUTE(obj)) return;

#if __DEBUG_REFCOUNT
    DEBUG_PRINT_CHAR("Increased reference for: %s (%p) to %d\n", object_debug(obj), obj, obj->ref_count);
#endif
}


/**
 * Decrease reference from object. Returns the current ref_count, or 0 on error, release or otherwise.
 */
static long object_dec_ref(t_object *obj) {
    if (! obj) return 0;

    if (obj->ref_count == 0) {
        fprintf(stderr, "sanity check failed: ref-count of object %p\n", obj);
        abort();
    }
    obj->ref_count--;

#ifdef __DEBUG
    ht_replace_ptr(refcount_objects, (void *)obj, (void *)(intptr_t)obj->ref_count);
#endif

#if __DEBUG_REFCOUNT
    if (! OBJECT_IS_CALLABLE(obj) && ! OBJECT_IS_ATTRIBUTE(obj)) {
        DEBUG_PRINT_CHAR("Decreased reference for: %s (%08X) to %d\n", object_debug(obj), (intptr_t)obj, obj->ref_count);
    }
#endif

    if (obj->ref_count != 0) {
        return obj->ref_count;
    }

#if __DEBUG_FREE_OBJECT
    if (! OBJECT_IS_CALLABLE(obj) && ! OBJECT_IS_ATTRIBUTE(obj)) {
        DEBUG_PRINT_CHAR("*** Freeing object %s (%08X)\n", object_debug(obj), (intptr_t)obj);
    }
#endif

    // Don't free static objects
    if (! OBJECT_IS_ALLOCATED(obj)) {
#if __DEBUG_FREE_OBJECT
        DEBUG_PRINT_CHAR("*** Not freeing static object %s\n", object_debug(obj));
#endif
        return 0;
    }

    // Free object
    _object_free(obj);
    return 0;
}

#ifdef __DEBUG
char *object_debug(t_object *obj) {
    if (! obj) {
        return "no object";
    }

    if (obj->__debug_info_available == 0) {
        return "no object info available";
    }

    if (! obj->funcs || ! obj->funcs->debug) {
        snprintf(obj->__debug_info, DEBUG_INFO_SIZE-1, "%s[%c](%s)", objectTypeNames[obj->type], OBJECT_TYPE_IS_CLASS(obj) ? 'C' : 'I', obj->name);
        return obj->__debug_info;
    }

    return obj->funcs->debug(obj);
}
#endif


/**
 * Clones an object and returns this new object
 */
t_object *object_clone(t_object *orig_obj) {
    assert(orig_obj != NULL);

    // Allocate new object with correct size and copy data
    t_object *clone_obj = smm_malloc(sizeof(t_object) + orig_obj->data_size);
    memcpy(clone_obj, orig_obj, sizeof(t_object) + orig_obj->data_size);

    // New separated object gets refcount 0
    clone_obj->ref_count = 0;
    clone_obj->name = string_strdup0(orig_obj->name);

    if (clone_obj->class) {
        object_inc_ref(clone_obj->class);
    }
    if (clone_obj->parent) {
        object_inc_ref(clone_obj->parent);
    }

    // Duplicate interfaces
    object_duplicate_interfaces(orig_obj, clone_obj);

    // Duplicate attributes (as-is) from original object
    object_duplicate_attributes(orig_obj, clone_obj);


    // If there is a custom clone function on the object, call it..
    if (orig_obj->funcs && orig_obj->funcs->clone) {
        clone_obj->funcs->clone(orig_obj, clone_obj);
    }

    return clone_obj;
}

/**
 * Duplicate attributes of a class into an instance. Will not add them directly
 * to the instance object, but just returns a hash-table with the objects
 *
 * @param src_obj    Class object to duplicate from
 * @param dst_obj    Instance object to duplicate the attributes to
 * @return
 */
static void object_duplicate_attributes(t_object *src_obj, t_object *dst_obj) {
    if (! src_obj->attributes) return;

    t_hash_table *duplicated_attributes = ht_create();

    t_hash_iter iter;
    ht_iter_init(&iter, src_obj->attributes);
    while (ht_iter_valid(&iter)) {
        char *name = ht_iter_key_str(&iter);
        t_attrib_object *attrib = ht_iter_value(&iter);

        // Duplicate attribute into new instance
        t_attrib_object *dup_attrib = object_attrib_duplicate(attrib, dst_obj);

        // We "bind" the attribute to this class
        object_attrib_bind(dup_attrib, dst_obj, name);

        // Replace the current attribute with the dupped one
        ht_add_str(duplicated_attributes, name, (void *)dup_attrib);

        // Increase reference to the duplicated attribute
        object_inc_ref((t_object *)dup_attrib);

        ht_iter_next(&iter);
    }

    // Store attributes into instance object
    dst_obj->attributes = duplicated_attributes;
}


/**
 * Returns a hash-string of the given object. By default this is the address of the object, but it could
 * also be a more customized hash function within an object itself.
 */
char *object_get_hash(t_object *obj) {
    // When there is no hash function, we just use the address of the object
    if (! obj->funcs->hash) {
        char *tmp = (char *)smm_malloc(32);
        snprintf(tmp, 31, "%ld", (long)obj);
        return tmp;
    }

    // Return objects hash
    return obj->funcs->hash(obj);
}



/**
 * Creates a new object with specific values, with a already created
 * argument DLL list.
 *
 * @param obj           The actual class to allocate from.
 * @param arguments     DLL with given arguments
 * @param *cached       If not NULL, it is set to 1 when the object was found in the cache.
 */
static t_object *_object_allocate_with_args(t_object *obj, t_dll *arguments, int *cached) {
    t_object *res = NULL;

    if (! OBJECT_TYPE_IS_CLASS(obj)) {
        fatal_error(1, "Can only _object_allocate_with_args() from a class.\n");
    }

    // Nothing found to new, just return NULL object
    if (! obj || ! obj->funcs) {
        RETURN_NULL;
    }

    // If we have a caching function, seek inside that cache first
    if (obj->funcs->cache) {
        res = obj->funcs->cache(obj, arguments);
        if (res) {
            // Set cached flag when the caller wants it
            if (cached) {
                *cached = 1;
            }
            return res;
        }
    }

    // Nothing found in cache, create new object

    // Create new object
#ifdef __DEBUG
    // In debug mode, allocate 200 bytes extra for __debug_info
    res = smm_malloc(sizeof(t_object) + obj->data_size + DEBUG_INFO_SIZE);
    memcpy(res, obj, sizeof(t_object) + obj->data_size + DEBUG_INFO_SIZE);
#else
    res = smm_malloc(sizeof(t_object) + obj->data_size);
    memcpy(res, obj, sizeof(t_object) + obj->data_size);
#endif

    // Since we just allocated the object, it can always be destroyed
    res->flags |= OBJECT_FLAG_ALLOCATED;

    res->ref_count = 0;
    res->class = obj;
    res->name = string_strdup0(obj->name);

    // Populate values, if needed
    if (res->funcs->populate && arguments) {
        res->funcs->populate(res, arguments);
    }

    return res;
}


/**
 * Initialize all the (scalar) objects
 */
void object_init() {
    // Attrib cannot have any callables, as callable hasn't been initialized yet
    object_attrib_init();
    // Callable can only have attribs
    object_callable_init();
    object_base_init();
    object_interfaces_init();
    object_hash_init();

    object_string_init();
    object_boolean_init();
    object_null_init();
    object_numerical_init();
    object_regex_init();
    object_tuple_init();
    object_list_init();
    object_exception_init();
    object_meta_init();
}


/**
 * Finalize all the (scalar) objects
 */
void object_fini() {
    DEBUG_PRINT_CHAR("object fini\n");

    object_meta_fini();
    object_exception_fini();
    object_list_fini();
    object_tuple_fini();
    object_regex_fini();
    object_numerical_fini();
    object_null_fini();
    object_boolean_fini();
    object_string_fini();

    object_hash_fini();
    object_interfaces_fini();
    object_base_fini();
    object_callable_fini();
    object_attrib_fini();
}


/**
 * Parse arguments for a given object. Used mostly for parsing arguments from Saffire methods
 *
 * Normally called as:
 *      object_parse_arguments(SAFFIRE_METHOD_ARGS, "ss", &s1_obj, &s2_obj);
 *
 * Spec is a string with the following format:
 *
 *   s    a string object
 *   n    a numerical object
 *   N    a NULL object
 *   r    a regex object
 *   b    a boolean object
 *   o    any object
 *   |    optional arguments after this
 *   +    previous value may also be nullable (does not make sense with 'N')
 *
 * so:
 *
 *   'ss' must have two string objects as arguments
 *   'so' must have one string, and one generic object (could be a string too)
 *   'ss|n'  must have two strings and optionally a numerical object
 *   'n+|n'  must have a numerical (or a NULL), and optionally a numerical (but not a NULL)
 *
 * Will return 0 on ok, -1 on error
 */
static int _object_parse_arguments(t_dll *arguments, int convert_objects, const char *speclist, va_list dst_vars) {
    const char *ptr = speclist;
    int optional_argument = 0;
    t_objectype_enum type;
    int result = -1;

    // Point to first element
    t_dll_element *e = DLL_HEAD(arguments);

    // First, check if the number of elements equals (or is more) than the number of mandatory objects in the spec
    int cnt = 0;
    while (*ptr) {
        if (*ptr == '|') break;     // Optional character that does not imply an argument itself
        if (*ptr == '+') break;     // Optional character that does not imply an argument itself
        cnt++;
        ptr++;
    }
    if (arguments->size < cnt) {
        object_raise_exception(Object_ArgumentException, 1, "Error while parsing argument list: at least %d arguments are needed. Only %ld are given", cnt, arguments->size);
        result = -1;
        goto done;
    }

    // We know have have enough elements. Iterate the spec
    ptr = speclist;
    while (*ptr) {
        int nullable = 0;
        char c = *ptr; // Save current spec character
        ptr++;
        switch (c) {
            case 'n' : /* numerical */
                type = objectTypeNumerical;
                break;
            case 'N' : /* null */
                type = objectTypeNull;
                break;
            case 's' : /* string */
                type = objectTypeString;
                break;
            case 'r' : /* regex */
                type = objectTypeRegex;
                break;
            case 'b' : /* boolean */
                type = objectTypeBoolean;
                break;
            case 'o' : /* any object */
                type = objectTypeAny;
                break;
            case '|' : /* Everything after a | is optional */
                optional_argument = 1;
                continue;
                break;
            default :
                object_raise_exception(Object_SystemException, 1, "Error while parsing argument list: cannot parse argument: '%c'", c);
                result = -1;
                goto done;
                break;
        }

        // Check if the next value is a +, if so, we also accept nullable
        if (*ptr == '+') {
            nullable = 1;
            ptr++;
        }

        // Fetch the next object from the list. We must assume the user has added enough room
        void **storage_ptr = va_arg(dst_vars, void **);
        if (optional_argument == 0 && (!e || ! DLL_DATA_PTR(e))) {
            object_raise_exception(Object_ArgumentException, 1, "Error while fetching mandatory argument.");
            result = -1;
            goto done;
        }

        t_object *argument_obj = e ? DLL_DATA_PTR(e) : NULL;
        if (argument_obj &&
            type != objectTypeAny &&
            (
                (nullable == 0 && (type != argument_obj->type)) ||
                (nullable == 1 && (type != argument_obj->type && argument_obj->type != objectTypeNull))
            )
        ) {
            object_raise_exception(Object_ArgumentException, 1, "Error while parsing argument list: wanted a %s%s, but got a %s", objectTypeNames[type], (nullable?" or a null":""), objectTypeNames[argument_obj->type]);
            result = -1;
            goto done;
        }

        // No argument object found, leave the current storage_ptr alone
        if (! argument_obj) {
            goto next;
        }

        // Don't convert objects, return as-is
        if (convert_objects == 0) {
            *storage_ptr = argument_obj;
            goto next;
        }

        // Convert objects if possible
        switch (type) {
            case objectTypeNumerical :
                if (nullable && argument_obj->type == objectTypeNull) {
                    // Instead of null-object, return int 0
                    *storage_ptr = 0;
                } else {
                    *storage_ptr = (void *)((t_numerical_object *)argument_obj)->data.value;
                }
                break;
            case objectTypeBoolean :
                if (nullable && argument_obj->type == objectTypeNull) {
                    // Instead of null-object, return int 0
                    *storage_ptr = 0;
                } else {
                    *storage_ptr = (void *)((t_boolean_object *)argument_obj)->data.value;
                }
                break;
            case objectTypeString :
                if (nullable && argument_obj->type == objectTypeNull) {
                    // return null object
                    *storage_ptr = argument_obj;
                } else {
                    *storage_ptr = (void *)((t_string_object *)argument_obj)->data.value;
                }
                break;
            case objectTypeRegex :
                if (nullable && argument_obj->type == objectTypeNull) {
                    // return null object
                    *storage_ptr = argument_obj;
                } else {
                    *storage_ptr = (void *)((t_regex_object *)argument_obj)->data.regex;
                }
            default :
                // Unknown type, so just return as-is
                *storage_ptr = argument_obj;
                break;
        }

next:
        // Goto next element
        e = e ? DLL_NEXT(e) : NULL;
    }

    // Everything is ok
    result = 0;

    // General cleanup
done:
    return result;
}


/**
 * Parses arguments, converts scalar objects like string and numericals to actual char *, and longs
 */
int object_parse_arguments(t_dll *arguments, const char *speclist, ...) {
    va_list dst_vars;

    va_start(dst_vars, speclist);
    int ret = _object_parse_arguments(arguments, 1, speclist, dst_vars);
    va_end(dst_vars);

    return ret;
}

/**
 * Parses arguments, but returns only objects
 */
int object_parse_argument_objects(t_dll *arguments, const char *speclist, ...) {
    va_list dst_vars;

    va_start(dst_vars, speclist);
    int ret = _object_parse_arguments(arguments, 0, speclist, dst_vars);
    va_end(dst_vars);

    return ret;
}


/**
 * Adds interface to object (class)
 */
void object_add_interface(t_object *class, t_object *interface) {
    if (! OBJECT_TYPE_IS_CLASS(class)) {
        fatal_error(1, "Interface can only be added to a class\n");     /* LCOV_EXCL_LINE */
    }

    if (! OBJECT_TYPE_IS_INTERFACE(interface)) {
        fatal_error(1, "%s is not an interface\n", interface->name);        /* LCOV_EXCL_LINE */
    }

    // Initialize interfaces DLL if not done already
    if (! class->interfaces) {
        class->interfaces = dll_init();
    }

    // Add object to interface
    dll_append(class->interfaces, interface);
    object_inc_ref(interface);
}

/**
 * Create method- attribute that points to an INTERNAL (C) function
 */
void object_add_internal_method_attributes(t_hash_table *attributes, t_object *obj, char *name, int method_flags, int visibility, void *func) {
    // @TODO: Instead of NULL, we should be able to add our parameters. This way, we have a more generic way to deal with internal and external functions.
    t_callable_object *callable_obj = (t_callable_object *)object_alloc_instance(Object_Callable, 3, CALLABLE_CODE_INTERNAL, func, /* arguments */ NULL);

    t_attrib_object *attrib_obj = (t_attrib_object *)object_alloc_instance(Object_Attrib, 7, obj, name, ATTRIB_TYPE_METHOD, visibility, ATTRIB_ACCESS_RO, callable_obj, method_flags);

    /* We don't add the attributes directly to the obj, but we store them inside attributes. Otherwise we run into trouble bootstrapping the callable and attrib objects
     * (as we need to create callables during the creation of callables in callable_init, for instance). By storing them separately inside an attribute hash, and adding the
     * hash when we are finished with the object, it works (we can't do any calls to the callables in between, but we are not allowed to anyway). */
    ht_add_str(attributes, name, attrib_obj);
    object_inc_ref((t_object *)attrib_obj);
}

/**
 * Create method- attribute that points to an INTERNAL (C) function
 */
void object_add_internal_method(t_object *obj, char *name, int method_flags, int visibility, void *func) {
    object_add_internal_method_attributes(obj->attributes, obj, name, method_flags, visibility, func);
}

/**
 *
 */
void object_add_property(t_object *obj, char *name, int visibility, t_object *property) {
    t_attrib_object *attrib_obj = (t_attrib_object *)object_alloc_instance(Object_Attrib, 7, obj, name, ATTRIB_TYPE_PROPERTY, visibility, ATTRIB_ACCESS_RW, property, 0);

    ht_add_str(obj->attributes, name, attrib_obj);
    object_inc_ref((t_object *)attrib_obj);
}


/**
 *
 */
void object_add_constant(t_object *obj, char *name, int visibility, t_object *constant) {
    t_attrib_object *attrib_obj = (t_attrib_object *)object_alloc_instance(Object_Attrib, 7, obj, name, ATTRIB_TYPE_CONSTANT, visibility, ATTRIB_ACCESS_RO, constant, 0);

    if (ht_exists_str(obj->attributes, name)) {
        object_release((t_object *)attrib_obj);
        fatal_error(1, "Attribute '%s' already exists in object '%s'\n", name, obj->name);      /* LCOV_EXCL_LINE */
    }

    ht_add_str(obj->attributes, name, attrib_obj);
    object_inc_ref((t_object *)attrib_obj);
}





static void _object_remove_all_internal_interfaces(t_object *obj) {
    if (! obj->interfaces) return;

    t_dll_element *e = DLL_HEAD(obj->interfaces);
    while (e) {
        object_release(DLL_DATA_PTR(e));
        e = DLL_NEXT(e);
    }
}


/**
 * Clears up all attributes found in this object. Note: does NOT release the object's attributes hash-table!
 */
static void _object_remove_all_internal_attributes(t_object *obj) {
    t_hash_iter iter;

    ht_iter_init(&iter, obj->attributes);
    while (ht_iter_valid(&iter)) {
        object_release((t_object *)ht_iter_value(&iter));
        ht_iter_next(&iter);
    }
}


/**
 * Frees internal object data for the given object
 */
void object_free_internal_object(t_object *obj) {
    // Free attributes
    if (obj->attributes) {
        _object_remove_all_internal_attributes(obj);
        ht_destroy(obj->attributes);
    }

    // @TODO: MEDIUM: if interfaces are linked, we only need to clean up when   obj == obj->class ??
    // Remove interfaces
    if (obj->interfaces) {
        _object_remove_all_internal_interfaces(obj);
        dll_free(obj->interfaces);
    }
}



/**
 * Raises an exception
 */
void object_raise_exception(t_object *exception, int code, char *format, ...) {
    va_list args;
    char *buf;

    va_start(args, format);
    smm_vasprintf_char(&buf, format, args);
    va_end(args);

    thread_create_exception((t_exception_object *)exception, code, buf);
    smm_free(buf);
}


/**
 * Checks if arguments of callable object 1 and callable object 2 matches.
 */
static int _object_check_matching_arguments(t_callable_object *obj1, t_callable_object *obj2) {
    t_hash_table *ht1 = obj1->data.arguments;
    t_hash_table *ht2 = obj2->data.arguments;

    // Sanity check
    if (ht1->element_count != ht2->element_count) {
        return 0;
    }

    // No parameters found at all.
    if (ht1->element_count == 0) return 1;


    t_hash_iter iter1;
    t_hash_iter iter2;
    ht_iter_init(&iter1, ht1);
    ht_iter_init(&iter2, ht2);
    while (ht_iter_valid(&iter1)) {
        t_method_arg *arg1 = ht_iter_value(&iter1);
        t_method_arg *arg2 = ht_iter_value(&iter2);

        DEBUG_PRINT_STRING_ARGS("        - typehint1: '%-20s (%d)'  value1: '%-20s' \n", OBJ2STR(arg1->typehint), object_debug(arg1->value));
        DEBUG_PRINT_STRING_ARGS("        - typehint2: '%-20s (%d)'  value2: '%-20s' \n", OBJ2STR(arg2->typehint), object_debug(arg2->value));

        if (string_strcmp(arg1->typehint->data.value, arg2->typehint->data.value) != 0) {
            return 0;
        }

        ht_iter_next(&iter1);
        ht_iter_next(&iter2);
    }
    return 1;
}


/**
 * checks if the given object implements fully the given interface. Return 1 when so, 0 otherwise.
 */
static int _object_check_interface_implementations(t_object *obj, t_object *interface) {
    // ceci ne pas une interface
    if (! OBJECT_TYPE_IS_INTERFACE(interface)) {
        return 0;
    }

    // iterate all attributes from the interface
    t_hash_iter iter;
    ht_iter_init(&iter, interface->attributes);
    while (ht_iter_valid(&iter)) {
        char *key = ht_iter_key_str(&iter);
        t_attrib_object *attribute = (t_attrib_object *)ht_iter_value(&iter);
        DEBUG_PRINT_STRING_ARGS(ANSI_BRIGHTBLUE "    interface attribute '" ANSI_BRIGHTGREEN "%s" ANSI_BRIGHTBLUE "' : " ANSI_BRIGHTGREEN "%s" ANSI_RESET "\n", key, object_debug((t_object *)attribute));

        t_attrib_object *found_obj = (t_attrib_object *)object_attrib_find(obj, key);
        if (! found_obj) {
            thread_create_exception_printf((t_exception_object *)Object_TypeException, 1, "Class '%s' does not fully implement interface '%s', missing attribute '%s'", obj->name, interface->name, key);
            return 0;
        }

        DEBUG_PRINT_STRING_ARGS("     - Found object : %s\n", object_debug((t_object *)found_obj));
        DEBUG_PRINT_STRING_ARGS("     - Matching     : %s\n", object_debug((t_object *)attribute));

        if (found_obj->data.attr_type != attribute->data.attr_type ||
            found_obj->data.attr_visibility != attribute->data.attr_visibility ||
            found_obj->data.attr_access != attribute->data.attr_access) {
            thread_create_exception_printf((t_exception_object *)Object_TypeException, 1, "Class '%s' does not fully implement interface '%s', mismatching settings for attribute '%s'", obj->name, interface->name, key);
            return 0;
        }

        // If we are a callable, check arguments
        if (OBJECT_IS_CALLABLE(found_obj->data.attribute)) {
            DEBUG_PRINT_CHAR("     - Checking parameter signatures\n");
            if (!_object_check_matching_arguments((t_callable_object *)attribute->data.attribute, (t_callable_object *)found_obj->data.attribute)) {
                thread_create_exception_printf((t_exception_object *)Object_TypeException, 1, "Class '%s' does not fully implement interface '%s', mismatching argument list for attribute '%s'", obj->name, interface->name, key);
                return 0;
            }
        }

        ht_iter_next(&iter);
    }

    return 1;
}


/**
 * Iterates all interfaces found in this object, and see if the object actually implements it fully
 */
int object_check_interface_implementations(t_object *obj) {
    t_dll_element *e = DLL_HEAD(obj->interfaces);
    while (e) {
        t_object *interface = DLL_DATA_PTR(e);

        DEBUG_PRINT_CHAR(ANSI_BRIGHTBLUE "* Checking interface: %s" ANSI_RESET "\n", interface->name);

        if (! _object_check_interface_implementations(obj, interface)) {
            return 0;
        }
        e = DLL_NEXT(e);
    }

    // Everything fully implemented
    return 1;
}


/**
 * Iterates all interfaces found in this object, and see if the object actually implements it fully
 */
int object_has_interface(t_object *obj, const char *interface_name) {
    DEBUG_PRINT_CHAR("object_has_interface(%s)\n", interface_name);

    t_dll_element *e = obj->interfaces != NULL ? DLL_HEAD(obj->interfaces) : NULL;
    while (e) {
        t_object *interface = DLL_DATA_PTR(e);

        if (strcasecmp(interface->name, interface_name) == 0) {
            return 1;
        }
        e = DLL_NEXT(e);
    }

    // No, cannot find it
    return 0;
}




/**
 * Duplicate interface DLL from src into dst.
 */
static void object_duplicate_interfaces(t_object *src_obj, t_object *dst_obj) {
    if (! src_obj->interfaces) return;

    t_dll *interfaces = dll_init();
    t_dll_element *e = DLL_HEAD(src_obj->interfaces);
    while (e) {
        dll_append(interfaces, DLL_DATA_PTR(e));
        object_inc_ref(DLL_DATA_PTR(e));
        e = DLL_NEXT(e);
    }

    dst_obj->interfaces = interfaces;
}


/**
 * Allocates a class from the given object. This can only happen when the VM creates a new user class or interface.
 */
t_object *object_alloc_class(t_object *obj, int arg_count, ...) {
    va_list arg_list;

    if (! OBJECT_TYPE_IS_CLASS(obj)) {
        fatal_error(1, "Can only allocate a new class from another class.\n");
    }

    // Create argument DLL
    t_dll *arguments = dll_init();
    va_start(arg_list, arg_count);
    for (int i=0; i!=arg_count; i++) {
        dll_append(arguments, (void *)va_arg(arg_list, void *));
    }
    va_end(arg_list);

    // Create new object
    int cached = 0;
    t_object *new_obj = _object_allocate_with_args(obj, arguments, &cached);

    // Free argument DLL
    dll_free(arguments);

    return new_obj;
}


/**
 * Allocate a new instance from a specific object
 */
t_object *object_alloc_instance(t_object *obj, int arg_count, ...) {
    va_list arg_list;

    if (! OBJECT_TYPE_IS_CLASS(obj)) {
        fatal_error(1, "Can only allocate a new instance from another class.\n");
    }

    // Create argument DLL
    t_dll *arguments = dll_init();
    va_start(arg_list, arg_count);
    for (int i=0; i!=arg_count; i++) {
        dll_append(arguments, (void *)va_arg(arg_list, void *));
    }
    va_end(arg_list);

    // Create new object
    int cached = 0;
    t_object *instance_obj = _object_allocate_with_args(obj, arguments, &cached);

    // Free argument DLL
    dll_free(arguments);

    // It might be possible that object_alloc_args() returned a cached instance.
    if (cached == 1) return instance_obj;



    // Bind all attributes to the instance
    object_duplicate_attributes(instance_obj->class, instance_obj);

    // Duplicate interfaces
    // @TODO: MEDIUM: We should not duplicate interfaces, but merely link them. Only when we hit the "base" class,
    // (ie: instance == parent), we should free the interfaces
    object_duplicate_interfaces(instance_obj->class, instance_obj);

    // Object is now an instance, not a class
    instance_obj->flags &= ~OBJECT_TYPE_CLASS;
    instance_obj->flags |= OBJECT_TYPE_INSTANCE;

    return instance_obj;
}



/**
 * Releases an object. This object cannot be used with this specific reference.
 *
 * Whenever the reference count has decreased to 0, it means that nothing holds a reference to this object.
 * It will be added to the garbage collected to either be destroyed, OR when needed, it can be revived before
 * collection whenever somebody needs this specific object again.
 */
long object_release(t_object *obj) {
    return object_dec_ref(obj);
}
