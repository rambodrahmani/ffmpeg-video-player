/**
 *
 *   File:   moving_rectangle.c
 *           Originally seen at: https://wiki.libsdl.org/SDL_CreateTexture
 *
 *   Author: Rambod Rahmani <rambodrahmani@autistici.org>
 *           Created on 8/11/18.
 *
 **/

#include <stdio.h>
#include <SDL2/SDL.h>

/**
 * Entry point.
 *
 * @param  argc command line arguments counter.
 * @param  argv command line arguments.
 * @return      execution exit code.
 */
int main(int argc, char *argv[])
{
    // This is the physical window you see on your screen. One SDL_Window
    // represents one physical window on your screen. You can have as many
    // SDL_Windows as you like. The structs holds info about the window like
    // position, size, window state and window style.
    SDL_Window * window;

    // A structure that contains a rendering state.
    // The SDL_Renderer is basically what you use to render to the screen. The
    // renderer is usually tied to a window. One renderer can only render
    // within one window. The SDL_Renderer also contains info about the rending
    // itself like hardware acceleration and v-sync prevention.
    SDL_Renderer * renderer;

    /**
     * Think of SDL_Window as physical pixels, and SDL_Renderer and a place to
     * store settings/context.
     */

    // The SDL_Renderer, renders SDL_Texture. SDL_Texture are the pixel
    // information of one element. It's the new version of SDL_Surface which
    // is much the same. The difference is mostly that SDL_Surface is just a
    // struct containing pixel information, while SDL_Texture is an efficient,
    // driver-specific representation of pixel data.
    SDL_Texture * texture;

    // SDL_Event used to handle quit event on Ctrl + C
    SDL_Event event;

    // The simplest struct in SDL. It contains only four shorts. x, y which
    // holds the position and w, h which holds width and height.It's important
    // to note that 0, 0 is the upper-left corner in SDL. So a higher y-value
    // means lower, and the bottom-right corner will have the coordinate x + w,
    // y + h.
    SDL_Rect r;

    // Use this function to initialize the SDL library. This must be called
    // before using most other SDL functions.
    int ret = SDL_Init(SDL_INIT_VIDEO); // [1]
    if (ret < 0)
    {
        // Use this function to log a message with SDL_LOG_PRIORITY_ERROR.
        SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,   // the category of the message
                "Couldn't initialize SDL: %s", SDL_GetError()   // a printf() style message format string
        );
        return 3;
    }

    // If SDL initialized successfully, we'll want to create a window using
    // SDL_CreateWindow. The first argument sets the window's caption or this
    // part of the window. The next two arguments define the x and y position
    // the window is created in. Since we don't care where it is created, we
    // just put SDL_WINDOWPOS_UNDEFINED for the x and y position. The next two
    // arguments define the window's width and height. The last argument are
    // the creation flags. SDL_WINDOW_SHOWN makes sure the window is shown when
    // it is created. If there is an error, SDL_CreateWindow returns NULL. If
    // there's no window, we want to print out the error to the console.
    window = SDL_CreateWindow(
            "SDL_CreateTexture",        // the title of the window, in UTF-8 encoding
            SDL_WINDOWPOS_UNDEFINED,    // the x position of the window, SDL_WINDOWPOS_CENTERED, or SDL_WINDOWPOS_UNDEFINED
            SDL_WINDOWPOS_UNDEFINED,    // the y position of the window, SDL_WINDOWPOS_CENTERED, or SDL_WINDOWPOS_UNDEFINED
            1024,                       // the width of the window, in screen coordinates
            768,                        // the height of the window, in screen coordinates
            SDL_WINDOW_RESIZABLE        // 0, or one or more SDL_WindowFlags OR'd together
    );

    r.w = 100;
    r.h = 50;

    // Use this function to create a 2D rendering context for a window.
    renderer = SDL_CreateRenderer(
            window, // the window where rendering is displayed
            -1,     // the index of the rendering driver to initialize, or -1 to
                    // initialize the first one supporting the requested flags
            0       // 0, or one or more SDL_RendererFlags OR'd together
    );

    // Use this function to create a texture for a rendering context.
    texture = SDL_CreateTexture(
            renderer,                   // the rendering context
            SDL_PIXELFORMAT_RGBA8888,   // one of the enumerated values in SDL_PixelFormatEnum
            SDL_TEXTUREACCESS_TARGET,   // one of the enumerated values in SDL_TextureAccess
            1024,                       // the width of the texture in pixels
            768                         // the height of the texture in pixels
    );

    while (1)
    {
        // Use this function to poll for currently pending events.
        // Returns 1 if there is a pending event or 0 if there are none
        // available.
        // If event is not NULL, the next event is removed from the queue and
        // stored in the SDL_Event structure pointed to by event.
        SDL_PollEvent(&event);

        if (event.type == SDL_QUIT)
        {
            break;
        }

        r.x = rand()%1000;
        r.y = rand()%1000;

        // Use this function to set a texture as the current rendering target.
        // Returns 0 on success or a negative error code on failure; call
        // SDL_GetError() for more information.
        SDL_SetRenderTarget(
                renderer,   // the rendering context
                texture     // the targeted texture, which must be created with
                            // the SDL_TEXTUREACCESS_TARGET flag, or NULL for
                            // the default render target
        );

        // Use this function to set the color used for drawing operations
        // (Rect, Line and Clear).
        // See -> SDL_RenderClear()
        // rgb(0, 0, 0) = black
        SDL_SetRenderDrawColor(
                renderer,   // the rendering context
                0x00,       // r, the red value used to draw on the rendering target
                0x00,       // g, the green value used to draw on the rendering target
                0x00,       // b, the blue value used to draw on the rendering target
                0x00        // a, the alpha value used to draw on the rendering target;
                            // usually SDL_ALPHA_OPAQUE (255). Use 
                            // SDL_SetRenderDrawBlendMode to specify how the 
                            // alpha channel is used
        );

        // Use this function to clear the current rendering target with the
        // drawing color.
        SDL_RenderClear(renderer);

        // Use this function to draw a rectangle on the current rendering target.
        SDL_RenderDrawRect(
                renderer,   // the rendering context
                &r          // an SDL_Rect structure representing the rectangle 
                            // to draw, or NULL to outline the entire rendering target
        );

        SDL_SetRenderDrawColor(
                renderer,
                0xFF,
                0x00,
                0x00,
                0x00
        );

        // Use this function to fill a rectangle on the current rendering target
        // with the drawing color.
        SDL_RenderFillRect(
                renderer,   // the rendering context
                &r          // the SDL_Rect structure representing the rectangle
                            // to fill, or NULL for the entire rendering target
        );

        SDL_SetRenderTarget(renderer, NULL);

        // Use this function to copy a portion of the texture to the current
        // rendering target.
        SDL_RenderCopy(
                renderer,
                texture,
                NULL,
                NULL
        );

        // Use this function to update the screen with any rendering performed
        // since the previous call.
        SDL_RenderPresent(renderer);    // [2]

        // allow for cpu scheduling
        SDL_Delay(100);
    }

    // Use this function to destroy the rendering context for a window and free
    // associated textures.
    SDL_DestroyRenderer(renderer);

    // Close and destroy the window
    // If window is NULL, this function will return immediately after setting
    // the SDL error message to "Invalid window". See SDL_GetError().
    SDL_DestroyWindow(window);


    // Use this function to clean up all initialized subsystems. You should
    // call it upon all exit conditions.
    SDL_Quit(); // [3]

    return 0;
}

// [1]
/**
 * SDL_Init() simply forwards to calling SDL_InitSubSystem(). Therefore, the
 * two may be used interchangeably. Though for readability of your code
 * SDL_InitSubSystem() might be preferred.
 *
 * The file I/O and threading subsystems are initialized by default. You must
 * specifically initialize other subsystems if you use them in your application.
 *
 * Logging works without initialization, too.
 *
 * flags may be any of the following OR'd together:
 *
 * SDL_INIT_TIMER
 * SDL_INIT_AUDIO
 * SDL_INIT_VIDEO
 * SDL_INIT_JOYSTICK
 * SDL_INIT_HAPTIC
 * SDL_INIT_GAMECONTROLLER
 * SDL_INIT_EVENTS
 * SDL_INIT_EVERYTHING
 * SDL_INIT_NOPARACHUTE
 *
 * If you want to initialize subsystems separately you would call SDL_Init(0)
 * followed by SDL_InitSubSystem() with the desired subsystem flag.
 */

// [2]
/**
 * SDL's rendering functions operate on a backbuffer; that is, calling a
 * rendering function such as SDL_RenderDrawLine() does not directly put a line
 * on the screen, but rather updates the backbuffer. As such, you compose your
 * entire scene and present the composed backbuffer to the screen as a complete
 * picture.
 *
 * Therefore, when using SDL's rendering API, one does all drawing intended for
 * the frame, and then calls this function once per frame to present the final
 * drawing to the user.
 *
 * The backbuffer should be considered invalidated after each present; do not
 * assume that previous contents will exist between frames. You are strongly
 * ncouraged to call SDL_RenderClear() to initialize the backbuffer before
 * starting each new frame's drawing, even if you plan to overwrite every pixel.
 */

// [3]
/**
 * You should call this function even if you have already shutdown each
 * initialized subsystem with SDL_QuitSubSystem(). It is safe to call this
 * function even in the case of errors in initialization.
 *
 * If you start a subsystem using a call to that subsystem's init function
 * (for example SDL_VideoInit()) instead of SDL_Init() or SDL_InitSubSystem(),
 * then you must use that subsystem's quit function (SDL_VideoQuit()) to shut
 * it down before calling SDL_Quit().
 *
 * You can use this function with atexit() to ensure that it is run when your
 * application is shutdown, but it is not wise to do this from a library or
 * other dynamically loaded code.
 */

