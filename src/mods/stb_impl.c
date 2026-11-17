/* stb_image / stb_image_write, compiled once. Public domain (see the headers); they are the only
 * dependency of the mod system, and they are only used for PNGs a mod supplies or texdump writes. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
