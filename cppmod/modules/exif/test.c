#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>

// Module structure
typedef struct _module {
//    char *name;
//    char *desc;
//    t_object **objects;
    void (*init)(void);     // Called when module is initialized
    void (*fini)(void);     // Called when module is unloaded
} t_module;

void main(void) {
    void *handle;

    handle = dlopen("./exif.so", RTLD_LAZY);
    if (! handle) {
        printf("Cannot open exif.so: %s\n", dlerror());
        exit(1);
    }

    printf("Finding dlsym\n");
    dlerror();
    t_module *module_info = dlsym(handle, "_saffire_module_entry");

    if (! module_info) {
        printf("Cannot locate 'module_entry': %s\n", dlerror());
        exit(1);
    }

    printf("Init: %p\n", module_info->init);
    printf("Fini: %p\n", module_info->fini);

    printf("Init(): ");  module_info->init();
    printf("Fini(): ");  module_info->fini();

    dlclose(handle);
}
