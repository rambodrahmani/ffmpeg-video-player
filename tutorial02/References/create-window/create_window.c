/**
 *
 *   File:   create_window.c
 *           Originally seen at: https://wiki.libsdl.org/SDL_CreateWindow
 *           Refer to previous examples for uncommented lines of code.
 *
 *   Author: Rambod Rahmani <rambodrahmani@autistici.org>
 *           Created on 8/10/18.
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
    SDL_Window * window;                    // declare a pointer to an SDL_Window

    int ret = SDL_Init(SDL_INIT_VIDEO);     // initialize SDL2
    if (ret < 0)
    {
        SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "Couldn't initialize SDL: %s", SDL_GetError()
        );
        return 3;
    }

    // Create an application window with the following settings:
    window = SDL_CreateWindow(
            "An SDL2 window",                  // window title
            SDL_WINDOWPOS_UNDEFINED,           // initial x position
            SDL_WINDOWPOS_UNDEFINED,           // initial y position
            640,                               // width, in pixels
            480,                               // height, in pixels
            SDL_WINDOW_OPENGL                  // flags - see below
    );


    // Check that the window was successfully created
    if (window == NULL)
    {
        // In the case that the window could not be made...
        printf("Could not create window: %s\n", SDL_GetError());
        return 1;
    }

    // The window is open: could enter program loop here (see SDL_PollEvent())

    SDL_Delay(3000);  // Pause execution for 3000 milliseconds, for example

    // Close and destroy the window
    SDL_DestroyWindow(window);

    // Clean up
    SDL_Quit();

    return 0;
}

/**
 * On Apple's OS X you must set the NSHighResolutionCapable Info.plist property
 * to YES, otherwise you will not receive a High DPI OpenGL canvas.
 *
 * If the window is created with the SDL_WINDOW_ALLOW_HIGHDPI flag, its size
 * in pixels may differ from its size in screen coordinates on platforms with
 * high-DPI support (e.g. iOS and Mac OS X). Use SDL_GetWindowSize() to query
 * the client area's size in screen coordinates, and SDL_GL_GetDrawableSize()
 * or SDL_GetRendererOutputSize() to query the drawable size in pixels.
 *
 * If the window is set fullscreen, the width and height parameters w and h
 * will not be used. However, invalid size parameters (e.g. too large) may
 * still fail. Window size is actually limited to 16384 x 16384 for all
 * platforms at window creation.
 */

