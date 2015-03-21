#ifndef SAFFIRE_H
#define SAFFIRE_H

#include <string>

#ifdef __cplusplus
    extern "C" {
#endif

namespace Saffire {

class Arguments {
public:
    void parse(const char *format, ...);
};

class Interface;

class Object {
public:
    void addMethod(const char *name, Object(*func)(Arguments &));
    void addProperty(const char *name, Object obj);
    void addConstant(const char *name, Object obj);
    void setParent(Object parent);
    void setParent(const char *parent);

    void addInterface(Interface *interface);
    void addInterface(char *interface);

    void setCtor(void(*func)(Arguments &));
    void setDtor(void(*func)(void));
};

class Interface : public Object {
};

class ModuleImpl;

class Module {
private:
    ModuleImpl _impl;

public:
    Module(const char *name, const char *desc);
    void addObject(Object object, const char *name);
};


class String: public Object {
protected:
    std::string strval;
public:
    String();
    String(std::string val);
    String(const char *val);
    char *value();
};

class Numerical: public Object {
public:
    Numerical(long num);
    long value();
};

class Hash: public Object {
public:
    void append(Object key, Object val);
};

class List: public Object {
public:
    void append(Object object);
};


typedef struct _module_entry {
    Saffire::Module(*initfunc)(void);
    void (*finifunc)(void);
} module_entry;


#define SAFFIRE_MODULE(init, fini)  \
        extern "C" {    \
            Saffire::module_entry _saffire_module_entry  = {    \
                init,   \
                fini    \
            };      \
        }


} // Namespace Saffire


#ifdef __cplusplus
    }
#endif

#endif /* SAFFIRE_H */

