#include "saffire.h"
#include <stdio.h>

Saffire::Object greet_method(Saffire::Arguments &args) {
    Saffire::String s;
    args.parse("s", &s);

    return Saffire::String( std::string("hello ") + s.value() );
}

void goodbye_ctor(Saffire::Arguments &args) {
    printf("Hello from the goodbye ctor()!\n");
}

/**
 * A module initialization function only needs to create and return a Saffire::Module(). It can consist of zero or objects.
 */
Saffire::Module init(void) {
    // Define the actual module
    Saffire::Module module("hello_mod", "a simple module to demonstrate how easy it should be to connect custom code to saffire");

    // Define two objects and attach to our module
    Saffire::Object hello_object;
    Saffire::Object goodbye_object;

    module.addObject(hello_object, "hello");
    module.addObject(goodbye_object, "goodbye");


    /********************************************************
     * Hello class defines
     ********************************************************/

    // Set the parent class
    // hello_object.setParent("base");
    // Set an interface
    // hello_object.addInterface("iterator");
    // hello_object.addInterface("subscription");

    // Set constructor and destructor
    // hello_object.setCtor(ctor_func);
    // hello_object.setDtor(dtor_func);

    // Add methods
    hello_object.addMethod("greet", greet_method);

    // Add properties
    hello_object.addProperty("question", Saffire::String("The answer to life, the universe and everything"));
    hello_object.addProperty("answer", Saffire::Numerical(42));

    // Add constants
    hello_object.addConstant("pi", Saffire::Numerical(314));


    /********************************************************
     * Goodbye class defines
     ********************************************************/

    // Set the parent class
    goodbye_object.setParent(hello_object);

    // Set constructor and destructor
    goodbye_object.setCtor(goodbye_ctor);




    return module;
}

SAFFIRE_MODULE(init)