/**
*
*   File:   render_present.c
*           Originally seen at: https://wiki.libsdl.org/SDL_RenderPresent
*           Refer to previous examples for uncommented lines of code.
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
    SDL_Window * window;                    // declare a pointer to an SDL_Window

    SDL_Renderer * renderer;

    int ret = SDL_Init(SDL_INIT_VIDEO);     // initialize SDL2
    if (ret < 0)
    {
        SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "Couldn't initialize SDL: %s", SDL_GetError()
        );
        return 1;
    }

    // Create an application window with the following settings:
    window = SDL_CreateWindow(
            "An SDL2 window",           // window title
            SDL_WINDOWPOS_CENTERED,     // initial x position
            SDL_WINDOWPOS_CENTERED,     // initial y position
            512,                        // width, in pixels
            512,                        // height, in pixels
            SDL_WINDOW_OPENGL           // flags - see below
    );


    // Check that the window was successfully created
    if (window == NULL)
    {
        // In the case that the window could not be made...
        printf("Could not create window: %s\n", SDL_GetError());
        return 1;
    }

    /* We must call SDL_CreateRenderer in order for draw calls to affect this window. */
    renderer = SDL_CreateRenderer(window, -1, 0);

    /* Select the color for drawing. It is set to red here. */
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);

    /* Clear the entire screen to our selected color. */
    SDL_RenderClear(renderer);

    /* Up until now everything was drawn behind the scenes.
           This will show the new, red contents of the window. */
    SDL_RenderPresent(renderer);

    /* Give us time to see the window. */
    SDL_Delay(5000);

    // Close and destroy the renderer
    SDL_DestroyRenderer(renderer);

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

