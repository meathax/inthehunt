#ifndef WORLD_SETUP_H
#define WORLD_SETUP_H
#include <stdint.h>
void world_setup_menu_art(uint32_t *pixels, unsigned width, unsigned height);
/* UTF-8 default path is anchored to the installation, not the working folder. */
const char *world_default_directory(void);
int world_setup_screen(const char *directory);
int world_setup_import(const wchar_t *archive);
#endif
