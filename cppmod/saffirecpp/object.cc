#include <saffire.h>
#include "saffireImpl.h"

void Object::addMethod(const char *name, Object(*func)(Arguments &)) {
    printf("Object::addMethod()\n");
}

void Object::addProperty(const char *name, Object obj) {
    printf("Object::addProp()\n");
}

void Object::addConstant(const char *name, Object obj) {
    printf("Object::addConstant()\n");
}

void Object::setParent(Object parent) {
    printf("Object::setParent\n");
}

void Object::setParent(const char *parent) {
    printf("Object::setParent(char*)\n");
}

void Object::addInterface(Interface *interface) {
    printf("Object::addInterface()\n");
}

void Object::addInterface(char *interface){
    printf("Object::addInterface(char*)\n");
}

void Object::setCtor(void(*func)(Arguments &)) {
    printf("Object::setCtor()\n");
}

void Object::setDtor(void(*func)(void)) {
    printf("Object::setDtor()\n");
}

