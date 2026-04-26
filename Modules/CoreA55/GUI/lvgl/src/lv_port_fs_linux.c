#include "lvgl.h"
#include <stdio.h>

static void * fs_open_cb(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode)
{
    FILE * f = NULL;

    const char * real_path = path;

    // Strip off S: drive letter if present
    if(path[0] == 'S' && path[1] == ':') {
        real_path = path + 2; // skip "S:"
    }

    f = fopen(real_path, (mode & LV_FS_MODE_WR) ? "rb+" : "rb");
    return f;
}

static lv_fs_res_t fs_close_cb(lv_fs_drv_t * drv, void * file_p)
{
    fclose((FILE *)file_p);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read_cb(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br)
{
    *br = fread(buf, 1, btr, (FILE *)file_p);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_seek_cb(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence)
{
    fseek((FILE *)file_p, pos, whence);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_tell_cb(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p)
{
    *pos_p = ftell((FILE *)file_p);
    return LV_FS_RES_OK;
}

void lv_port_fs_init(void)
{
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);

    drv.letter = 'S'; // hook the drive letter S:
    drv.open_cb = fs_open_cb;
    drv.close_cb = fs_close_cb;
    drv.read_cb = fs_read_cb;
    drv.seek_cb = fs_seek_cb;
    drv.tell_cb = fs_tell_cb;

    lv_fs_drv_register(&drv);
}
