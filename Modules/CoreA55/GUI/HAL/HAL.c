
#include <stdio.h>  //for sprintf, etc.
#include <stdbool.h>

#include "HAL.h"


lv_indev_t *_GUI_InputDevice = NULL; //Needed because LVGL-9.1 doesn't seem to set indev_active and returns NULL for lv_indev_active()


/**
 * Initialize the Hardware Abstraction Layer (HAL) for the LVGL graphics
 * library
 */
void HAL_init (int input_id, int framebuffer_id) {
    int InputEventID = 0; //0; = touch screen 1; mouse
    int FrameBufferID = 0;
    char* TouchScreenDeviceName = NULL;
    char* MouseDeviceName = NULL; // Fixed: Added declaration
    static char TempString [32] = "";

    if (input_id >= 0) InputEventID = input_id;
    if (framebuffer_id >= 0) FrameBufferID = framebuffer_id;

    /*Create a display*/
    static lv_display_t *display;
#if (SQUARELINE_BUILD_TARGET__UNIX || SQUARELINE_BUILD_TARGET__WINDOWS || SQUARELINE_BUILD_TARGET__EMSCRIPTEN)
    display = lv_sdl_window_create( GUI_RESOLUTION_HORIZONTAL, GUI_RESOLUTION_VERTICAL );
#elif (SQUARELINE_BUILD_TARGET__BOARD || SQUARELINE_BUILD_TARGET__BOARD__CORE_A55)
    display = lv_linux_fbdev_create();
    sprintf( TempString, "/dev/fb%d", FrameBufferID );
    lv_linux_fbdev_set_file( display, TempString );
    //lv_linux_fbdev_set_force_refresh( disp, 1 ); //only use if really necessary! (In case of partial drawing and other issues.)
#endif


    //Add the mouse/touchscreen as input device
    //static lv_indev_t *pointer;
//#if (SQUARELINE_BUILD_TARGET__UNIX || SQUARELINE_BUILD_TARGET__WINDOWS || SQUARELINE_BUILD_TARGET__EMSCRIPTEN) // commented out because I want to use mouse MPO
//    _GUI_InputDevice /*pointer*/ = lv_sdl_mouse_create();
//#elif (SQUARELINE_BUILD_TARGET__BOARD || SQUARELINE_BUILD_TARGET__BOARD__CORE_A55)
    sprintf( TempString, "/dev/input/event%d", InputEventID );
    TouchScreenDeviceName = lv_libinput_find_dev( LV_LIBINPUT_CAPABILITY_TOUCH, true );
    if (TouchScreenDeviceName == NULL || input_id >= 0) { TouchScreenDeviceName = TempString; }
    _GUI_InputDevice /*pointer*/ = lv_libinput_create/*lv_evdev_create*/( LV_INDEV_TYPE_POINTER, TouchScreenDeviceName /*TempString*/ );
//#endif

// Change 1: Search for POINTER (Mouse) capability instead of TOUCH

 /*    MouseDeviceName = "/dev/input/event1";
    // Change 2: Ensure type is set to LV_INDEV_TYPE_POINTER
    _GUI_InputDevice = lv_libinput_create( LV_INDEV_TYPE_POINTER, MouseDeviceName );

    LV_LOG_WARN("Using input device: %s\n", MouseDeviceName);


    lv_indev_t *i = lv_indev_get_next(NULL);
    while (i) {
        LV_LOG_WARN("indev=%p type=%d\n", (void*)i, (int)lv_indev_get_type(i));
        i = lv_indev_get_next(i);
    }
    
    // Change 3: Enable a cursor (Mice need a visual pointer, touchscreens do not)
    lv_obj_t * cursor_obj = lv_label_create(lv_screen_active());
    lv_label_set_text(cursor_obj, "+");           // simple crosshair
    lv_obj_set_style_text_font(cursor_obj, LV_FONT_DEFAULT, 0);
    lv_indev_set_cursor(_GUI_InputDevice, cursor_obj); */
    
    /*Add the keyboard as an input device*/
    //static lv_indev_t *keyboard;
    //keyboard = lv_sdl_keyboard_create();
}


