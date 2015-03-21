#include "../saffire.h"
#include <stdio.h>
#include <libexif/exif-data.h>

// This is a model of our data class
class ExifObject public: Saffire::Object {
protected:
    ExifData *ed;
    Saffire::List *ed_list;
public:
    ExifObject() {
        ed = NULL;
        ed_list = NULL;
    }
    ~ExifObject() {
        exif_data_free(ed);
        destroy ed_list;
    }
}

static void foreach_entry(ExifEntry *entry, void *user) {
    Saffire::Hash hash = reinterpret_cast<Saffire::Hash &>(user);

    char buf[64];

    exif_entry_get_value(entry, buf, sizeof(buf));
    //printf("  %04X %-20s: '%s'\n", entry->tag, exif_tag_get_name(entry->tag), buf);

    hash.append(Saffire::String(exif_tag_get_name(entry->tag)), Saffire::String(buf));
}

static void foreach_content(ExifContent *content, void *user) {
    Saffire::List list = reinterpret_cast<Saffire::List &>(user);

    printf("Content!\n");

    Saffire::Hash hash;
    exif_content_foreach_entry(content, foreach_entry, reinterpret_cast<void *>(&hash));

    list.append(hash);
}

// exif.getTagName(0x100) -> string("ImageWidth")
Saffire::Object exif_getTagName(Saffire::Object *self, Saffire::Arguments &args) {
    Saffire::String tagname;
    args.parse("s", &tagname);

    return Saffire::String(exif_tag_get_name(tagname.value()));
}

Saffire::Object exif_data(Saffire::Object *self, Saffire::Arguments &args) {
    // Already created
    if (self.ed_list != NULL) {
        return self.ed_list;
    }

    // Create new list
    self.ed_list = Saffire::List();
    exif_data_foreach_content(ed, foreach_content, reinterpret_cast<void *>(&self.ed_list));
    return self.ed_list;
}


void exif_ctor_func(Saffire::Object *self, Saffire::Arguments &args) {
    printf("Hello from the goodbye ctor()!\n");

    Saffire::String s;
    args.parse("s", &s);

    self.ed = exif_data_new_from_file(s.value());
}

/**
 * A module initialization function only needs to create and return a Saffire::Module(). It can consist of zero or objects.
 */
Saffire::Module _init(void) {
    printf("exif_init()\n");

    // Define the actual module
    Saffire::Module exif_module("exif", "Exif for Saffire");

    // Define two objects and attach to our module
    Saffire::ExifObject exif_object();

    exif_module.addObject(exif_object, "exif");


    /********************************************************
     * Exif class defines
     ********************************************************/

    // Set constructor and destructor
    exif_object.setCtor(exif_ctor_func);

    // Add methods
    exif_object.addMethod("data", exif_data);
    exif_object.addMethod("getTagName", exif_getTagName);

    return exif_module;
}

void _fini(void) {
    printf("exif_fini()\n");
}

SAFFIRE_MODULE(_init, _fini)
