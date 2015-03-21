#ifndef SAFFIRE_IMPL_H
#define SAFFIRE_IMPL_H

#ifdef __cplusplus
    extern "C" {
#endif

namespace Saffire {


class ModuleImlp {
protected:

public:
    ModuleImpl(const char *name, const char *desc);
    void addObject(Object object, const char *name);
};


} // Namespace Saffire

#ifdef __cplusplus
    }
#endif

#endif /* SAFFIRE_H */

