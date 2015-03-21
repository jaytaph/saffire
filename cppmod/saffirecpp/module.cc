#include <saffire.h>
#include "saffireImpl.h"

void Module(const char *name, const char *desc) {
    _impl = ModuleImpl(name, desc);
}

void Module::addObject(Object object, const char *name) {
    _impl.addObject(object, name);
}
