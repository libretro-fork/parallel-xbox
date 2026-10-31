/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "ui/console.h"
#include "qemu/memfd.h"
#include "standard-headers/drm/drm_fourcc.h"
#include "trace.h"

pixman_format_code_t qemu_default_pixman_format(int bpp, bool native_endian)
{
    if (native_endian) {
        switch (bpp) {
        case 15:
            return PIXMAN_x1r5g5b5;
        case 16:
            return PIXMAN_r5g6b5;
        case 24:
            return PIXMAN_r8g8b8;
        case 32:
            return PIXMAN_x8r8g8b8;
        }
    } else {
        switch (bpp) {
        case 24:
            return PIXMAN_b8g8r8;
        case 32:
            return PIXMAN_b8g8r8x8;
        break;
        }
    }
    return 0;
}



void qemu_pixman_image_unref(pixman_image_t *image)
{
    if (image == NULL) {
        return;
    }
    pixman_image_unref(image);
}


static void *
qemu_pixman_shareable_alloc(const char *name, size_t size,
                            qemu_pixman_shareable *handle,
                            Error **errp)
{
#ifdef WIN32
    return qemu_win32_map_alloc(size, handle, errp);
#else
    return qemu_memfd_alloc(name, size, 0, handle, errp);
#endif
}

static void
qemu_pixman_shareable_free(qemu_pixman_shareable handle,
                           void *ptr, size_t size)
{
#ifdef WIN32
    Error *err = NULL;

    qemu_win32_map_free(ptr, handle, &err);
    if (err) {
        error_report_err(err);
    }
#else
    qemu_memfd_free(ptr, size, handle);
#endif
}

static void
qemu_pixman_shared_image_destroy(pixman_image_t *image, void *data)
{
    qemu_pixman_shareable handle = PTR_TO_SHAREABLE(data);
    void *ptr = pixman_image_get_data(image);
    size_t size = pixman_image_get_height(image) * pixman_image_get_stride(image);

    qemu_pixman_shareable_free(handle, ptr, size);
}

bool
qemu_pixman_image_new_shareable(pixman_image_t **image,
                                qemu_pixman_shareable *handle,
                                const char *name,
                                pixman_format_code_t format,
                                int width,
                                int height,
                                int rowstride_bytes,
                                Error **errp)
{
    ERRP_GUARD();
    size_t size = height * rowstride_bytes;
    void *bits = NULL;

    g_return_val_if_fail(image != NULL, false);
    g_return_val_if_fail(handle != NULL, false);

    bits = qemu_pixman_shareable_alloc(name, size, handle, errp);
    if (!bits) {
        return false;
    }

    *image = pixman_image_create_bits(format, width, height, bits, rowstride_bytes);
    if (!*image) {
        error_setg(errp, "Failed to allocate image");
        qemu_pixman_shareable_free(*handle, bits, size);
        return false;
    }

    pixman_image_set_destroy_function(*image,
                                      qemu_pixman_shared_image_destroy,
                                      SHAREABLE_TO_PTR(*handle));

    return true;
}
