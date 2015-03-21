#include <stdio.h>
#include <libexif/exif-data.h>


static void foreach_entry(ExifEntry *entry) {
    char buf[64];
    exif_entry_get_value(entry, buf, sizeof(buf));
    printf("  %04X %-20s: '%s'\n", entry->tag, exif_tag_get_name(entry->tag), buf);
}

static void foreach_content(ExifContent *content) {
    printf("Content!\n");
    exif_content_foreach_entry(content, (void *)foreach_entry, NULL);
}



void main(int argc, char *argv[]) {
    ExifData *ed = exif_data_new_from_file(argv[1]);

    exif_data_foreach_content(ed, (void *)foreach_content, NULL);

}
