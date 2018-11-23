/**
*
*   File:   tutorial02-deprecated.c
*           So our current plan is to replace the SaveFrame() function from
*           Tutorial 1, and instead output our frame to the screen. But first
*           we have to start by seeing how to use the SDL Library.
*           Uncommented lines of code refer to previous tutorials.
*
*           Compiled using
*               gcc -o tutorial02-deprecated tutorial02-deprecated.c -lavutil -lavformat -lavcodec -lswscale -lz -lm  `sdl-config --cflags --libs`
*           on Arch Linux.
*
*           sdl-config just prints out the proper flags for gcc to include the
*           SDL libraries properly. You may need to do something different to
*           get it to compile on your system; please check the SDL documentation
*           for your system. Once it compiles, go ahead and run it.
*
*           What happens when you run this program? The video is going crazy!
*           In fact, we're just displaying all the video frames as fast as we
*           can extract them from the movie file. We don't have any code right
*           now for figuring out when we need to display video. Eventually (in
*           Tutorial 5), we'll get around to syncing the video. But first we're
*           missing something even more important: sound!
*
*   Author: Rambod Rahmani <rambodrahmani@autistici.org>
*           Created on 8/10/18.
*
**/

#include <unistd.h>
#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL.h>
#include <SDL_thread.h>

void printHelpMenu();

/**
 * Entry point.
 *
 * @param   argc    command line arguments counter.
 * @param   argv    command line arguments.
 *
 * @return          execution exit code.
 */
int main(int argc, char * argv[])
{
    if ( !(argc > 2) )
    {
        printHelpMenu();
        return -1;
    }

    int ret = -1;

    /**
     * SDL.
     * Deprecated: this implementation uses deprecated SDL functionalities.
     * Plese refer to tutorial02.c for an implementation using the new
     * SDL API.
     */
    ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);   // [1]
    if (ret != 0)
    {
        // error while initializing SDL
        printf("Could not initialize SDL - %s\n.", SDL_GetError());

        // exit with error
        return -1;
    }

    AVFormatContext * pFormatCtx = NULL;
    ret = avformat_open_input(&pFormatCtx, argv[1], NULL, NULL);
    if (ret < 0)
    {
        printf("Could not open file %s\n", argv[1]);
        return -1;
    }

    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if (ret < 0)
    {
        printf("Could not find stream information %s\n", argv[1]);
        return -1;
    }

    av_dump_format(pFormatCtx, 0, argv[1], 0);

    int i;
    AVCodecContext * pCodecCtx = NULL;

    int videoStream = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++)
    {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = i;
            break;
        }
    }

    if (videoStream == -1)
    {
        printf("Could not find video stream.");
        return -1;
    }

    AVCodec * pCodec = NULL;
    pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
    if (pCodec == NULL)
    {
        printf("Unsupported codec!\n");
        return -1;
    }

    pCodecCtx = avcodec_alloc_context3(pCodec);
    ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
    if (ret != 0)
    {
        printf("Could not copy codec context.\n");
        return -1;
    }

    ret = avcodec_open2(pCodecCtx, pCodec, NULL);
    if (ret < 0)
    {
        printf("Could not open codec.\n");
        return -1;
    }

    AVFrame * pFrame = NULL;
    pFrame = av_frame_alloc();
    if (pFrame == NULL)
    {
        printf("Could not allocate frame.\n");
        return -1;
    }

    // Now we need a place on the screen to put stuff. The basic area for
    // displaying images with SDL is called a surface:
    SDL_Surface *screen;    // [2]
    #ifndef __DARWIN__
            screen = SDL_SetVideoMode(pCodecCtx->width/2, pCodecCtx->height/2, 0, 0);   // [3]
    #else
            screen = SDL_SetVideoMode(pCodecCtx->width/2, pCodecCtx->height/2, 24, 0);  // [3]
    #endif
    if (!screen)
    {
        // could not set video mode
        printf("SDL: could not set video mode - exiting.\n");

        // exit with Error
        return -1;
    }

    struct SwsContext * sws_ctx = NULL;
    AVPacket * pPacket = av_packet_alloc();
    if (pPacket == NULL)
    {
        printf("Could not alloc packet,\n");
        return -1;
    }

    // Now we create a YUV overlay on that screen so we can input video to it,
    SDL_Overlay * bmp = NULL;   // [4]
    bmp = SDL_CreateYUVOverlay( // [5]
        pCodecCtx->width,
        pCodecCtx->height,
        SDL_YV12_OVERLAY,
        screen
    );

    // and set up our SWSContext to convert the image data to YUV420:
    sws_ctx = sws_getContext(
        pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        pCodecCtx->width,
        pCodecCtx->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );

    /**
     * As we said before, we are using YV12 to display the image, and getting
     * YUV420 data from ffmpeg.
     */

    int maxFramesToDecode;
    sscanf(argv[2], "%d", &maxFramesToDecode);

    // used later to handle quit event
    SDL_Event event;    // [8]

    i = 0;
    while (av_read_frame(pFormatCtx, pPacket) >= 0)
    {
        if (pPacket->stream_index == videoStream)
        {
            ret = avcodec_send_packet(pCodecCtx, pPacket);
            if (ret < 0)
            {
                printf("Error sending packet for decoding.\n");
                return -1;
            }

            while (ret >= 0)
            {
                ret = avcodec_receive_frame(pCodecCtx, pFrame);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                else if (ret < 0)
                {
                    printf("Error while decoding.\n");
                    return -1;
                }

                /**
                 * Well that was simple enough! Now we just need to display the
                 * image. Let's go all the way down to where we had our
                 * finished frame. We can get rid of all that stuff we had for
                 * the RGB frame, and we're going to replace the SaveFrame()
                 * with our display code. To display the image, we're going to
                 * make an AVPicture struct and set its data pointers and
                 * linesize to our YUV overlay:
                 */

                // First, we lock the overlay because we are going to be
                // writing to it. This is a good habit to get into so you don't
                // have problems later.
                SDL_LockYUVOverlay(bmp);    // [6]

                AVPicture pict;

                // [7]
                pict.data[0] = bmp->pixels[0];
                pict.data[1] = bmp->pixels[2];
                pict.data[2] = bmp->pixels[1];

                pict.linesize[0] = bmp->pitches[0];
                pict.linesize[1] = bmp->pitches[2];
                pict.linesize[2] = bmp->pitches[1];

                // Convert the image into YUV format that SDL uses:
                // We change the conversion format to PIX_FMT_YUV420P, and we
                // use sws_scale just like before.
                sws_scale(
                    sws_ctx,
                    (uint8_t const * const *)pFrame->data,
                    pFrame->linesize,
                    0,
                    pCodecCtx->height,
                    pict.data,
                    pict.linesize
                );

                // The opposite to SDL_LockYUVOverlay. Unlocks a previously
                // locked overlay. An overlay must be unlocked before it can be
                // displayed.
                SDL_UnlockYUVOverlay(bmp);

                if (++i <= maxFramesToDecode)
                {
                    // get clip fps
                    double fps = av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate);

                    // get clip sleep time
                    double sleep_time = 1.0/(double)fps;

                    // sleep
                    usleep(sleep_time);

                    SDL_Rect rect;
                    rect.x = 0;
                    rect.y = 0;
                    rect.w = pCodecCtx->width/2;
                    rect.h = pCodecCtx->height/2;

                    // Blit the overlay to the display surface specified when the
                    // overlay was created.
                    SDL_DisplayYUVOverlay(bmp, &rect);  // [9]

                    printf(
                        "Frame %c (%d) pts %d dts %d key_frame %d [coded_picture_number %d, display_picture_number %d, %dx%d]\n",
                        av_get_picture_type_char(pFrame->pict_type),
                        pCodecCtx->frame_number,
                        pFrame->pts,
                        pFrame->pkt_dts,
                        pFrame->key_frame,
                        pFrame->coded_picture_number,
                        pFrame->display_picture_number,
                        pCodecCtx->width,
                        pCodecCtx->height
                    );
                }
                else
                {
                    break;
                }
            }

            if (i > maxFramesToDecode)
            {
                break;
            }
        }

        av_packet_unref(pPacket);

        /**
         * Let's take this time to show you another feature of SDL: its event
         * system. SDL is set up so that when you type, or move the mouse in the
         * SDL application, or send it a signal, it generates an event. Your
         * program then checks for these events if it wants to handle user
         * input. Your program can also make up events to send the SDL event
         * system. This is especially useful when multithread programming with
         * SDL, which we'll see in Tutorial 4. In our program, we're going to
         * poll for events right after we finish processing a packet. For now,
         * we're just going to handle the SDL_QUIT event so we can exit:
         */
        SDL_PollEvent(&event);
        switch(event.type)
        {
            case SDL_QUIT:
            {
                SDL_Quit();
                exit(0);
            }
            break;

            default:
            {
                // nothing to do
            }
            break;
        }
    }

    /**
     * Cleanup.
     */
    av_frame_free(&pFrame);
    av_free(pFrame);

    avcodec_close(pCodecCtx);

    avformat_close_input(&pFormatCtx);

    return 0;
}

/**
 * Print help menu containing usage information.
 */
void printHelpMenu()
{
    printf("Invalid arguments.\n\n");
    printf("Usage: ./tutorial02-deprecated <filename> <max-frames-to-decode>\n\n");
    printf("e.g: ./tutorial02-deprecated /home/rambodrahmani/Videos/video.mp4 200\n");
}

// [1]
/**
 * SDL_Init() essentially tells the library what features we're going to use.
 * SDL_GetError(), of course, is a handy debugging function.
 *
 * Use this function to initialize the SDL library. This must be called before
 * using most other SDL functions.
 *
 * https://wiki.libsdl.org/SDL_Init
 */

// [2]
/**
 * This sets up a screen with the given width and height. The next option is
 * the bit depth of the screen - 0 is a special value that means "same as the
 * current display". (This does not work on OS X).
 *
 * A structure that contains a collection of pixels used in software blitting.
 *
 * With most surfaces you can access the pixels directly. Surfaces that have
 * been optimized with SDL_SetSurfaceRLE() should be locked with
 * SDL_LockSurface() before accessing pixels. When you are done you should call
 * SDL_UnlockSurface() before blitting.
 *
 * https://wiki.libsdl.org/SDL_Surface
 */

// [3]
/**
 * This sets up a screen with the given width and height. The next option is
 * the bit depth of the screen - 0 is a special value that means "same as the
 * current display". (This does not work on OS X; see source.)
 */

// [4]
/**
 * A SDL_Overlay is similar to a SDL_Surface except it stores a YUV overlay.
 * All the fields are read only, except for pixels which should be locked
 * before use.
 *
 * https://www.libsdl.org/release/SDL-1.2.15/docs/html/sdloverlay.html
 *
 * The format field stores the format of the overlay which is one of the
 * following:
 * #define SDL_YV12_OVERLAY  0x32315659  -> Planar mode: Y + V + U
 * #define SDL_IYUV_OVERLAY  0x56555949  -> Planar mode: Y + U + V
 * #define SDL_YUY2_OVERLAY  0x32595559  -> Packed mode: Y0+U0+Y1+V0
 * #define SDL_UYVY_OVERLAY  0x59565955  -> Packed mode: U0+Y0+V0+Y1
 * #define SDL_YVYU_OVERLAY  0x55595659  -> Packed mode: Y0+V0+Y1+U0
 */

// [5]
/**
 * SDL_CreateYUVOverlay creates a YUV overlay of the specified width, height
 * and format (see SDL_Overlay for a list of available formats), for the
 * provided display. A SDL_Overlay structure is returned.
 *
 * http://sdl.beuc.net/sdl.wiki/SDL_CreateYUVOverlay
 */

// [6]
/**
 * Much the same as SDL_LockSurface, SDL_LockYUVOverlay locks the overlay for
 * direct access to pixel data.
 *
 * http://sdl.beuc.net/sdl.wiki/SDL_LockYUVOverlay
 */

// [7]
/**
 * The AVPicture struct, as shown before, has a data pointer that is an array
 * of 4 pointers. Since we are dealing with YUV420P here, we only have 3
 * channels, and therefore only 3 sets of data. Other formats might have a
 * fourth pointer for an alpha channel or something. linesize is what it sounds
 * like. The analogous structures in our YUV overlay are the pixels and pitches
 * variables. ("pitches" is the term SDL uses to refer to the width of a given
 * line of data.) So what we do is point the three arrays of pict.data at our
 * overlay, so when we write to pict, we're actually writing into our overlay,
 * which of course already has the necessary space allocated. Similarly, we get
 * the linesize information directly from our overlay.
 */

// [8]
/**
 * The SDL_Event union is the core to all event handling is SDL, its probably
 * the most important structure after SDL_Surface. SDL_Event is a union of all
 * event structures used in SDL, using it is a simple matter of knowing which
 * union member relates to which event type.
 */

// [9]
/**
 * Blit the overlay to the display surface specified when the overlay was created.
 * The SDL_Rect structure, dstrect, specifies a rectangle on the display where the
 * overlay is drawn. The .x and .y fields of dstrect specify the upper left location
 * in display coordinates. The overlay is scaled (independently in x and y dimensions)
 * to the size specified by dstrect, and is optimized for 2x scaling.
 *
 * Returns 0 on success.
 *
 * Bit blit (also written BITBLT, BIT BLT, BitBLT, Bit BLT, Bit Blt etc., which
 * stands for bit block transfer) is a data operation commonly used in computer
 * graphics in which several bitmaps are combined into one using a boolean function.
 * The operation involves at least two bitmaps, one source and destination, possibly
 * a third that is often called the "mask" and sometimes a fourth used to create
 * a stencil. The pixels of each are combined bitwise according to the specified
 * raster operation (ROP) and the result is then written to the destination. The
 * ROP is essentially a boolean formula. The most obvious ROP overwrites the
 * destination with the source. Other ROPs may involve AND, OR, XOR, and NOT
 * operations.
 *
 * The Commodore Amiga's graphics chipset, for example, could combine three source
 * bitmaps according to any of 256 boolean functions of three variables.
 * Modern graphics software has almost completely replaced bitwise operations with
 * more general mathematical operations used for effects such as alpha compositing.
 * This is because bitwise operations on color displays do not usually produce results
 * that resemble the physical combination of lights or inks.
 *
 * The name derives from the BitBLT routine for the Xerox Alto computer, standing
 * for bit-boundary block transfer. Dan Ingalls, Larry Tesler, Bob Sproull, and
 * Diana Merry programmed this operation at Xerox PARC in November 1975 for the
 * Smalltalk-72 system. Dan Ingalls later implemented a redesigned version in microcode.
 * The development of fast methods for various bit blit operations gave impetus to
 * the evolution of computer displays from using character graphics to using bitmap
 * graphics for everything. Machines that rely heavily on the performance of 2D
 * graphics (such as video game consoles) often have special-purpose circuitry
 * called a blitter.
 */