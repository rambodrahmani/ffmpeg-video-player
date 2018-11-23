/**
*
*   File:   tutorial04-deprecated.c
*           Last time we added audio support by taking advantage of SDL's audio
*           functions. SDL started a thread that made callbacks to a function we
*           defined every time it needed audio. Now we're going to do the same
*           sort of thing with the video display. This makes the code more
*           modular and easier to work with - especially when we want to add
*           syncing.
*           Go ahead and compile it: and enjoy your unsynced movie! Next time
*           we'll finally build a video player that actually works!
*
*           Compiled using
*               gcc -o tutorial04-deprecated tutorial04-deprecated.c -lavutil -lavformat -lavcodec -lswscale -lz -lm  `sdl-config --cflags --libs`
*           on Arch Linux.
*
*           Please refer to previous tutorials for uncommented lines of code.
*
*   Author: Rambod Rahmani <rambodrahmani@autistici.org>
*           Created on 8/18/18.
*
**/

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/avstring.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

#ifdef __MINGW32__
#undef main
#endif

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

/**
 * We now have a max size for our audio and video queue.
 */
#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

/**
 * We get values for user events by using the SDL constant SDL_USEREVENT. The
 * first user event should be assigned the value SDL_USEREVENT, the next
 * SDL_USEREVENT + 1, and so on.
 *
 * Events SDL_USEREVENT through SDL_MAXEVENTS-1 are for your use.
 */
#define FF_REFRESH_EVENT (SDL_USEREVENT)    // [1]
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1

/**
 * This queue structure is used to store AVPackets.
 */
typedef struct PacketQueue
{
    AVPacketList *  first_pkt;
    AVPacketList *  last_pkt;
    int             nb_packets;
    int             size;
    SDL_mutex *     mutex;
    SDL_cond *      cond;
} PacketQueue;

/**
 * This queue structure is used to store processed video frames.
 */
typedef struct VideoPicture
{
    SDL_Overlay *   bmp;
    int             width;
    int             height;
    int             allocated;
} VideoPicture;

/**
 * We're going to clean up the code a bit. We have all this audio and video codec
 * information, and we're going to be adding queues and buffers and who knows what
 * else. All this stuff is for one logical unit, viz. the movie. So we're going
 * to make a large struct that will hold all that information called the VideoState.
 *
 * Here we see a glimpse of what we're going to get to. First we see the basic
 * information - the format context and the indices of the audio and video stream,
 * and the corresponding AVStream objects. Then we can see that we've moved some
 * of those audio buffers into this structure. These (audio_buf, audio_buf_size,
 * etc.) were all for information about audio that was still lying around (or the
 * lack thereof). We've added another queue for the video, and a buffer (which will
 * be used as a queue; we don't need any fancy queueing stuff for this) for the
 * decoded frames (saved as an overlay). The VideoPicture struct is of our own
 * creations (we'll see what's in it when we come to it). We also notice that we've
 * allocated pointers for the two extra threads we will create, and the quit flag
 * and the filename of the movie.
 */
typedef struct VideoState
{
    /**
     * File I/O Context.
     */
    AVFormatContext * pFormatCtx;

    /**
     * Audio Stream.
     */
    int                 audioStream;
    AVStream *          audio_st;
    AVCodecContext *    audio_ctx;
    PacketQueue         audioq;
    uint8_t             audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) /2];
    unsigned int        audio_buf_size;
    unsigned int        audio_buf_index;
    AVFrame             audio_frame;
    AVPacket            audio_pkt;
    uint8_t *           audio_pkt_data;
    int                 audio_pkt_size;

    /**
     * Video Stream.
     */
    int                 videoStream;
    AVStream *          video_st;
    AVCodecContext *    video_ctx;
    PacketQueue         videoq;
    struct swsContext * sws_ctx;
    VideoPicture        pictq[VIDEO_PICTURE_QUEUE_SIZE];
    int                 pictq_size;
    int                 pictq_rindex;
    int                 pictq_windex;
    SDL_mutex *         pictq_mutex;
    SDL_cond *          pictq_cond;

    /**
     * Threads.
     */
    SDL_Thread *    decode_tid;
    SDL_Thread *    video_tid;

    /**
     * Input file name.
     */
    char filename[1024];

    /**
     * Global quit flag.
     */
    int quit;
} VideoState;

/**
 * Global SDL_Surface reference.
 */
SDL_Surface * screen;

/**
 * Global SDL_Surface mutex.
 */
SDL_mutex * screen_mutex;

/**
 * Global VideoState reference.
 */
VideoState * global_video_state;

/**
 * Methods declaration.
 */
void printHelpMenu();

int decode_thread(void * arg);

int stream_component_open(
        VideoState * videoState,
        int stream_index
);

void alloc_picture(void * userdata);

int queue_picture(
        VideoState * is,
        AVFrame * pFrame
);

int video_thread(void *arg);

void video_refresh_timer(void * userdata);

static void schedule_refresh(
        VideoState * videoState,
        int delay
);

static Uint32 sdl_refresh_timer_cb(
        Uint32 interval,
        void * opaque
);

void video_display(VideoState * videoState);

void packet_queue_init(PacketQueue * q);

int packet_queue_put(
        PacketQueue * queue,
        AVPacket * packet
);

static int packet_queue_get(
        PacketQueue * queue,
        AVPacket * packet,
        int blocking
);

void audio_callback(
        void * userdata,
        Uint8 * stream,
        int len
);

int audio_decode_frame(
        VideoState * videoState,
        uint8_t * audio_buf,
        int buf_size
);

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
    // if the given number of command line arguments videoState wrong,
    if ( !(argc == 3) )
    {
        // print help menu and exit
        printHelpMenu();
        return -1;
    }

    int ret = -1;

    // Register all formats and codecs
    av_register_all();

    /**
     * SDL.
     * Deprecated: this implementation uses deprecated SDL functionalities.
     */
    ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
    if (ret != 0)
    {
        printf("Could not initialize SDL - %s\n.", SDL_GetError());
        return -1;
    }

    // Now we need a place on the screen to put stuff. The basic area for
    // displaying images with SDL is called a surface:
    #ifndef __DARWIN__
        screen = SDL_SetVideoMode(640, 480, 0, 0);
    #else
        screen = SDL_SetVideoMode(640, 480, 24, 0);
    #endif
    if (!screen)
    {
        // could not set video mode
        printf("SDL: could not set video mode - exiting.\n");
        return -1;
    }

    screen_mutex = SDL_CreateMutex();

    // global VideoState reference will be set in decode_thread()
    VideoState * videoState = NULL;

    // av_mallocz() is a nice function that will allocate memory for us and zero it out.
    videoState = av_mallocz(sizeof(VideoState));    // [2]

    // copy the file name input to the VideoState structure
    av_strlcpy(videoState->filename, argv[1], sizeof(videoState->filename));    // [3]

    /**
     * Then we'll initialize our locks for the display buffer (pictq), because
     * since the event loop calls our display function - the display function,
     * remember, will be pulling pre-decoded frames from pictq. At the same time,
     * our video decoder will be putting information into it - we don't know who
     * will get there first. Hopefully you recognize that this is a classic race
     * condition. So we allocate it now before we start any threads.
     */
    videoState->pictq_mutex = SDL_CreateMutex();
    videoState->pictq_cond = SDL_CreateCond();

    // Now let's finally launch our threads and get the real work done: schedule_refresh
    // is a function we will define later. What it basically does is tell the system
    // to push a FF_REFRESH_EVENT after the specified number of milliseconds.
    // This will in turn call the video refresh function when we see it in the event
    // queue. But for now, let's look at SDL_CreateThread().
    schedule_refresh(videoState, 40);

    // SDL_CreateThread() does just that - it spawns a new thread that has complete
    // access to all the memory of the original process, and starts the thread
    // running on the function we give it. It will also pass that function user-defined
    // data. In this case, we're calling decode_thread() and with our VideoState
    // struct attached.
    videoState->decode_tid = SDL_CreateThread(decode_thread, videoState);    // [4]
    if(!videoState->decode_tid)
    {
        printf("Could not create SDL_Thread - exiting.\n");
        av_free(videoState);
        return -1;
    }

    // infinite loop waiting for fired events
    SDL_Event event;
    for(;;)
    {
        SDL_WaitEvent(&event);
        switch(event.type)
        {
            case FF_QUIT_EVENT:
            case SDL_QUIT:
            {
                videoState->quit = 1;
                SDL_Quit();
            }
            break;

            case FF_REFRESH_EVENT:
            {
                video_refresh_timer(event.user.data1);
            }
            break;

            default:
            {
                // nothing to do
            }
            break;
        }

        // check global quit flag
        if (videoState->quit)
        {
            // exit for loop
            break;
        }
    }

    return 0;
}

/**
 * Print help menu containing usage information.
 */
void printHelpMenu()
{
    printf("Invalid arguments.\n\n");
    printf("Usage: ./tutorial04-deprecated <filename> <max-frames-to-decode>\n\n");
    printf("e.g: ./tutorial04-deprecated /home/rambodrahmani/Videos/video.mp4 200\n");
}

/**
 * Opens Audio and Video Streams. If all codecs are retrieved correctly, starts
 * an infinite loop to read AVPackets from the global VideoState AVFormatContext.
 * Based on their stream index, each packet is placed in the appropriate queue.
 *
 * @param   arg
 *
 * @return      < 0 in case of error, 0 otherwise.
 */
int decode_thread(void * arg)
{
    VideoState * videoState = (VideoState *)arg;

    int ret = 0;

    // file I/O context: demuxers read a media file and split it into chunks of data (packets)
    AVFormatContext * pFormatCtx = NULL;
    ret = avformat_open_input(&pFormatCtx, videoState->filename, NULL, NULL);
    if (ret < 0)
    {
        printf("Could not open file %s.\n", videoState->filename);
        return -1;
    }

    AVPacket pkt1, *packet = &pkt1;

    videoState->videoStream = -1;
    videoState->audioStream = -1;

    // set global VideoState reference
    global_video_state = videoState;

    /**
     * The first half of the function has nothing new; it simply does the work of
     * opening the file and finding the index of the audio and video streams. The
     * only thing we do different is save the format context in our big struct.
     */
    videoState->pFormatCtx = pFormatCtx;

    // read packets of a media file to get stream information
    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if (ret < 0)
    {
        printf("Could not find stream information %s.\n", videoState->filename);
        return -1;
    }

    // Dump information about file onto standard error
    av_dump_format(pFormatCtx, 0, videoState->filename, 0);

    // video and audio stream indexes
    int videoStream = -1;
    int audioStream = -1;

    // loop through the streams that have been found
    for (int i = 0; i < pFormatCtx->nb_streams; i++)
    {
        // look for video stream
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0)
        {
            videoStream = i;
        }

        // look for audio stream
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStream < 0)
        {
            audioStream = i;
        }
    }

    /**
     * After we've found our stream indices, we call another function that we
     * will define, stream_component_open().
     */

    // return with error in case no video stream was found
    if (videoStream == -1)
    {
        printf("Could not find video stream.\n");
        goto fail;
    }
    else
    {
        // open stream component codec
        ret = stream_component_open(videoState, videoStream);

        // check video codec was opened correctly
        if (ret < 0)
        {
            printf("Could not open video codec.");
            goto fail;
        }
    }

    // return with error in case no audio stream was found
    if (audioStream == -1)
    {
        printf("Could not find audio stream.\n");
        goto fail;
    }
    else
    {
        // open stream component codec
        ret = stream_component_open(videoState, audioStream);

        // check audio codec was opened correctly
        if (ret < 0)
        {
            printf("Could not open audio codec.");
            goto fail;
        }
    }

    if (videoState->videoStream < 0 || videoState->audioStream < 0)
    {
        printf("Could not open codecs.\n", videoState->filename);
        goto fail;
    }

    // main decode loop: it's basically just a for loop that will read in a packet
    // and put it on the right queue
    for (;;)
    {
        // check global quit flag
        if (videoState->quit)
        {
            break;
        }

        /**
         * Nothing really new here, except that we now have a max size for our
         * audio and video queue, and we've added a check for read errors. The
         * format context has a ByteIOContext struct inside it called pb. ByteIOContext
         * is the structure that basically keeps all the low-level file information
         * in it.
         */

        // seek stuff goes here
        if (videoState->audioq.size > MAX_AUDIOQ_SIZE || videoState->videoq.size > MAX_VIDEOQ_SIZE)
        {
            SDL_Delay(10);
            continue;
        }

        // read data from the AVFormatContext by repeatedly calling av_read_frame()
        if (av_read_frame(videoState->pFormatCtx, packet) < 0)
        {
            if (videoState->pFormatCtx->pb->error == 0)
            {
                /* no error; wait for user input */
                SDL_Delay(100);

                continue;
            }
            else
            {
                // exit for loop in case of error
                break;
            }
        }

        // put the packet in the appropriate queue
        if (packet->stream_index == videoState->videoStream)
        {
            packet_queue_put(&videoState->videoq, packet);
        }
        else if (packet->stream_index == videoState->audioStream)
        {
            packet_queue_put(&videoState->audioq, packet);
        }
        else
        {
            /* Deprecated, please refer to tutorial04-resampled.c for the new API */
            // otherwise free the memory
            av_free_packet(packet);
        }
    }

    /**
     * After our for loop, we have all the code for waiting for the rest of the
     * program to end or informing it that we've ended. This code is instructive
     * because it shows us how we push events - something we'll have to later to
     * display the video.
     */
    while (!videoState->quit)
    {
        SDL_Delay(100);
    }

    /**
     * In case of failure, push the FF_QUIT_EVENT and return.
     */
    fail:
    {
        if (1)
        {
            // create FF_QUIT_EVENT
            SDL_Event event;
            event.type = FF_QUIT_EVENT;
            event.user.data1 = videoState;

            // push FF_QUIT_EVENT
            SDL_PushEvent(&event);  // [5]

            // return with error
            return -1;
        }
    };

    return 0;
}

/**
 * This is a pretty natural way to split things up, and since we do a lot of similar
 * things to set up the video and audio codec, we reuse some code by making this a
 * function.
 *
 * The stream_component_open() function is where we will find our codec decoder,
 * set up our audio options, save important information to our big struct, and
 * launch our audio and video threads. This is where we would also insert other
 * options, such as forcing the codec instead of autodetecting it and so forth.
 *
 * @param   videoState      The global VideoState reference used to save info
 *                          related to the media being played.
 * @param   stream_index    The stream index obtained from the AVFormatContext.
 *
 * @return                  < 0 in case of error, 0 otherwise.
 */
int stream_component_open(VideoState * videoState, int stream_index)
{
    /**
     * This is pretty much the same as the code we had before, except now it's generalized
     * for audio and video. Notice that instead of aCodecCtx, we've set up our big struct
     * as the userdata for our audio callback. We've also saved the streams themselves
     * as audio_st and video_st. We also have added our video queue and set it up in
     * the same way we set up our audio queue. Most of the point is to launch the
     * video and audio threads.
     */

    // retrieve file I/O context
    AVFormatContext * pFormatCtx = videoState->pFormatCtx;

    // check the given stream index if valid
    if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams)
    {
        printf("Invalid stream index.");
        return -1;
    }

    // retrieve codec
    AVCodec * codec = NULL;
    codec = avcodec_find_decoder(pFormatCtx->streams[stream_index]->codecpar->codec_id);
    if (codec == NULL)
    {
        printf("Unsupported codec.\n");
        return -1;
    }

    // retrieve codec context
    AVCodecContext * codecCtx = NULL;
    codecCtx = avcodec_alloc_context3(codec);
    int ret = avcodec_parameters_to_context(codecCtx, pFormatCtx->streams[stream_index]->codecpar);
    if (ret != 0)
    {
        printf("Could not copy codec context.\n");
        return -1;
    }

    if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        // desired and obtained audio specs references
        SDL_AudioSpec wanted_specs;
        SDL_AudioSpec specs;

        // Set audio settings from codec info
        wanted_specs.freq = codecCtx->sample_rate;
        wanted_specs.format = AUDIO_S16SYS;
        wanted_specs.channels = codecCtx->channels;
        wanted_specs.silence = 0;
        wanted_specs.samples = SDL_AUDIO_BUFFER_SIZE;
        wanted_specs.callback = audio_callback;
        wanted_specs.userdata = videoState;

        /* Deprecated, please refer to tutorial04-resampled.c for the new API */
        // open audio device
        ret = SDL_OpenAudio(&wanted_specs, &specs);

        // check audio device correctly opened
        if (ret < 0)
        {
            fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
            return -1;
        }
    }

    // initialize the AVCodecContext to use the given AVCodec
    if (avcodec_open2(codecCtx, codec, NULL) < 0)
    {
        printf("Unsupported codec!\n");
        return -1;
    }

    // set the global VideoState based on the typed of the codec obtained from
    // the given stream index
    switch (codecCtx->codec_type)
    {
        case AVMEDIA_TYPE_AUDIO:
        {
            // set VideoState audio related fields
            videoState->audioStream = stream_index;
            videoState->audio_st = pFormatCtx->streams[stream_index];
            videoState->audio_ctx = codecCtx;
            videoState->audio_buf_size = 0;
            videoState->audio_buf_index = 0;

            memset(&videoState->audio_pkt, 0, sizeof(videoState->audio_pkt));

            // init audio packet queue
            packet_queue_init(&videoState->audioq);

            // start playing audio on the first audio device
            SDL_PauseAudio(0);
        }
        break;

        case AVMEDIA_TYPE_VIDEO:
        {
            // set VideoState video related fields
            videoState->videoStream = stream_index;
            videoState->video_st = pFormatCtx->streams[stream_index];
            videoState->video_ctx = codecCtx;

            // init video packet queue
            packet_queue_init(&videoState->videoq);

            // start video thread
            videoState->video_tid = SDL_CreateThread(video_thread, videoState);

            // set up the VideoState SWSContext to convert the image data to YUV420
            videoState->sws_ctx = sws_getContext(videoState->video_ctx->width,
                                         videoState->video_ctx->height,
                                         videoState->video_ctx->pix_fmt,
                                         videoState->video_ctx->width,
                                         videoState->video_ctx->height,
                                         AV_PIX_FMT_YUV420P,
                                         SWS_BILINEAR,
                                         NULL,
                                         NULL,
                                         NULL
            );
        }
        break;

        default:
        {
            // nothing to do
        }
        break;
    }

    return 0;
}

/**
 * You should recognize the SDL_CreateYUVOverlay function that we've moved from
 * our main loop to this section. This code should be fairly self-explanitory by
 * now. However, now we have a mutex lock around it because two threads cannot
 * write information to the screen at the same time! This will prevent our alloc_picture
 * function from stepping on the toes of the function that will display the
 * picture. (We've created this lock as a global variable and initialized it in
 * main(); see code.) Remember that we save the width and height in the VideoPicture
 * structure because we need to make sure that our video size doesn't change for
 * some reason.
 *
 * @param userdata
 */
void alloc_picture(void * userdata)
{
    VideoState * videoState = (VideoState *)userdata;
    VideoPicture * vp;

    vp = &videoState->pictq[videoState->pictq_windex];
    if (vp->bmp)
    {
        // we already have one make another, bigger/smaller
        SDL_FreeYUVOverlay(vp->bmp);    // [6]
    }

    // Allocate a place to put our YUV image on that screen
    SDL_LockMutex(screen_mutex);

    vp->bmp = SDL_CreateYUVOverlay(videoState->video_ctx->width,
                                   videoState->video_ctx->height,
                                   SDL_YV12_OVERLAY,
                                   screen);
    SDL_UnlockMutex(screen_mutex);

    vp->width = videoState->video_ctx->width;
    vp->height = videoState->video_ctx->height;
    vp->allocated = 1;
}

/**
 * Let's look at the function that stores our decoded frame, pFrame in our picture
 * queue. Since our picture queue is an SDL overlay (presumably to allow the video
 * display function to have as little calculation as possible), we need to convert
 * our frame into that. The data we store in the picture queue is a struct of our
 * making.
 *
 * To use this queue, we have two pointers - the writing index and the reading index.
 * We also keep track of how many actual pictures are in the buffer. To write to
 * the queue, we're going to first wait for our buffer to clear out so we have space
 * to store our VideoPicture. Then we check and see if we have already allocated
 * the overlay at our writing index. If not, we'll have to allocate some space.
 * We also have to reallocate the buffer if the size of the window has changed!
 *
 * @param videoState
 * @param pFrame
 * @return
 */
int queue_picture(VideoState * videoState, AVFrame * pFrame)
{
    VideoPicture * vp;
    int dst_pix_fmt;
    AVPicture pict;

    /* wait until we have space for a new pic */
    SDL_LockMutex(videoState->pictq_mutex);

    while (videoState->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !videoState->quit)
    {
        SDL_CondWait(videoState->pictq_cond, videoState->pictq_mutex);
    }

    SDL_UnlockMutex(videoState->pictq_mutex);

    // check global quit flag
    if (videoState->quit)
    {
        return -1;
    }

    // windex is set to 0 initially
    vp = &videoState->pictq[videoState->pictq_windex];

    /* allocate or resize the buffer! */
    if (
        !vp->bmp ||
        vp->width != videoState->video_ctx->width ||
        vp->height != videoState->video_ctx->height
    )
    {
        vp->allocated = 0;
        alloc_picture(videoState);

        // check global quit flag
        if(videoState->quit)
        {
            return -1;
        }
    }

    /**
     * Okay, we're all settled and we have our YUV overlay allocated and ready to
     * receive a picture.
     */

    /* We have a place to put our picture on the queue */
    if (vp->bmp)
    {
        /**
         * The majority of this part is simply the code we used earlier to fill
         * the YUV overlay with our frame. The last bit is simply "adding" our
         * value onto the queue. The queue works by adding onto it until it is
         * full, and reading from it as long as there is something on it. Therefore
         * everything depends upon the is->pictq_size value, requiring us to lock
         * it. So what we do here is increment the write pointer (and rollover if
         * necessary), then lock the queue and increase its size. Now our reader
         * will know there is more information on the queue, and if this makes our
         * queue full, our writer will know about it.
         */
        SDL_LockYUVOverlay(vp->bmp);

        dst_pix_fmt = AV_PIX_FMT_YUV420P;
        /* point pict at the queue */

        /* Deprecated, please refer to tutorial04-resampled.c for the new API */
        pict.data[0] = vp->bmp->pixels[0];
        pict.data[1] = vp->bmp->pixels[2];
        pict.data[2] = vp->bmp->pixels[1];

        pict.linesize[0] = vp->bmp->pitches[0];
        pict.linesize[1] = vp->bmp->pitches[2];
        pict.linesize[2] = vp->bmp->pitches[1];

        // Convert the image into YUV format that SDL uses
        sws_scale(videoState->sws_ctx,
                (uint8_t const * const *)pFrame->data,
                pFrame->linesize,
                0,
                videoState->video_ctx->height,
                pict.data,
                pict.linesize
        );

        SDL_UnlockYUVOverlay(vp->bmp);

        /* now we inform our display thread that we have a pic ready */
        if(++videoState->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE)
        {
            videoState->pictq_windex = 0;
        }

        SDL_LockMutex(videoState->pictq_mutex);

        videoState->pictq_size++;

        SDL_UnlockMutex(videoState->pictq_mutex);
    }

    return 0;
}

/**
 * This thread reads in packets from the video queue, decodes the video into frames,
 * and then calls a queue_picture function to put the processed frame onto a picture
 * queue.
 *
 * Most of this function should be familiar by this point. We've moved our
 * avcodec_decode_video2 function here, just replaced some of the arguments; for
 * example, we have the AVStream stored in our big struct, so we get our codec from
 * there. We just keep getting packets from our video queue until someone tells
 * us to quit or we encounter an error.
 *
 * @param arg
 *
 * @return
 */
int video_thread(void * arg)
{
    VideoState * videoState = (VideoState *)arg;

    AVPacket pkt1;
    AVPacket *packet = &pkt1;

    int frameFinished;

    // alloc the AVFrame used to decode video packets
    AVFrame * pFrame;
    pFrame = av_frame_alloc();

    for (;;)
    {
        if (packet_queue_get(&videoState->videoq, packet, 1) < 0)
        {
            // means we quit getting packets
            break;
        }

        /* Deprecated, please refer to tutorial04-resampled.c for the new API */
        // Decode video frame
        avcodec_decode_video2(videoState->video_ctx, pFrame, &frameFinished, packet);

        // Did we get a video frame?
        if (frameFinished)
        {
            if(queue_picture(videoState, pFrame) < 0)
            {
                break;
            }
        }

        /* Deprecated, please refer to tutorial04-resampled.c for the new API */
        av_free_packet(packet);
    }

    av_frame_free(&pFrame);

    return 0;
}

/**
 * For now, this is a pretty simple function: it pulls from the queue when we have
 * something, sets our timer for when the next video frame should be shown, calls
 * video_display to actually show the video on the screen, then increments the
 * counter on the queue, and decreases its size. You may notice that we don't
 * actually do anything with vp in this function, and here's why: we will. Later.
 * We're going to use it to access timing information when we start syncing the
 * video to the audio. See where it says "timing code here"? In that section,
 * we're going to figure out how soon we should show the next video frame, and
 * then input that value into the schedule_refresh() function. For now we're just
 * putting in a dummy value of 80. Technically, you could guess and check this
 * value, and recompile it for every movie you watch, but 1) it would drift after
 * a while and 2) it's quite silly. We'll come back to it later, though.
 *
 * @param   userdata    SDL_UserEvent->data1;   User defined data pointer.
 */
void video_refresh_timer(void * userdata)
{
    VideoState * videoState = (VideoState *)userdata;
    VideoPicture * vp;

    if (videoState->video_st)
    {
        if (videoState->pictq_size == 0)
        {
            schedule_refresh(videoState, 1);
        }
        else
        {
            vp = &videoState->pictq[videoState->pictq_rindex];

            /**
             * Now, normally here goes a ton of code about timing, etc. we're just
             * going to guess at a delay for now. You can increase and decrease
             * this value and hard code the timing - but I don't suggest that ;)
             * We'll learn how to do it for real later.
             */

            schedule_refresh(videoState, 40);

            /* show the picture! */
            video_display(videoState);

            /* update queue for next picture! */
            if(++videoState->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE)
            {
                videoState->pictq_rindex = 0;
            }

            SDL_LockMutex(videoState->pictq_mutex);

            videoState->pictq_size--;

            SDL_CondSignal(videoState->pictq_cond);

            SDL_UnlockMutex(videoState->pictq_mutex);
        }
    }
    else
    {
        schedule_refresh(videoState, 100);
    }
}

/**
 * SDL_AddTimer() is an SDL function that simply makes a callback to the user-specfied
 * function after a certain number of milliseconds (and optionally carrying some
 * user data). We're going to use this function to schedule video updates - every
 * time we call this function, it will set the timer, which will trigger an event,
 * which will have our main() function in turn call a function that pulls a frame
 * from our picture queue and displays it! Phew!
 *
 * @param   videoState
 * @param   delay
 */
static void schedule_refresh(VideoState * videoState, int delay)
{
    SDL_AddTimer(delay, sdl_refresh_timer_cb, videoState);  // [7]
}

/**
 * Here is the now-familiar event push. FF_REFRESH_EVENT is defined here as
 * SDL_USEREVENT + 1. One thing to notice is that when we return 0, SDL stops
 * the timer so the callback is not made again.
 *
 * @param   interval
 * @param   opaque
 *
 * @return
 */
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void * opaque)
{
    // create FF_REFRESH_EVENT event
    SDL_Event event;
    event.type = FF_REFRESH_EVENT;
    event.user.data1 = opaque;

    // push FF_REFRESH_EVENT event
    SDL_PushEvent(&event);

    /* 0 means stop timer */
    return 0;
}

/**
 * Since our screen can be of any size (we set ours to 640x480 and there are
 * ways to set it so it is resizable by the user), we need to dynamically figure
 * out how big we want our movie rectangle to be. So first we need to figure out
 * our movie's aspect ratio, which is just the width divided by the height. Some
 * codecs will have an odd sample aspect ratio, which is simply the width/height
 * radio of a single pixel, or sample. Since the height and width values in our
 * codec context are measured in pixels, the actual aspect ratio is equal to the
 * aspect ratio times the sample aspect ratio. Some codecs will show an aspect
 * ratio of 0, and this indicates that each pixel is simply of size 1x1. Then we
 * scale the movie to fit as big in our screen as we can. The & -3 bit-twiddling
 * in there simply rounds the value to the nearest multiple of 4. Then we center
 * the movie, and call SDL_DisplayYUVOverlay(), making sure we use the screen
 * mutex to access it.
 *
 * @param videoState
 */
void video_display(VideoState * videoState)
{
    SDL_Rect rect;

    VideoPicture * vp;

    float aspect_ratio;

    int w, h, x, y;
    int i;

    // get next VideoPicture to be displayed from the VideoPicture queue
    vp = &videoState->pictq[videoState->pictq_rindex];

    if (vp->bmp)
    {
        if (videoState->video_ctx->sample_aspect_ratio.num == 0)
        {
            aspect_ratio = 0;
        }
        else
        {
            aspect_ratio = av_q2d(videoState->video_ctx->sample_aspect_ratio) * videoState->video_ctx->width / videoState->video_ctx->height;
        }

        if (aspect_ratio <= 0.0)
        {
            aspect_ratio = (float)videoState->video_ctx->width /
                           (float)videoState->video_ctx->height;
        }

        // global SDL_Surface height
        h = screen->h;

        w = ((int)rint(h * aspect_ratio)) & -3;

        if (w > screen->w)
        {
            // global SDL_Surface width
            w = screen->w;

            // if width changed, recalculate height using the aspect ratio
            h = ((int)rint(w / aspect_ratio)) & -3;
        }

        x = (screen->w - w) / 2;
        y = (screen->h - h) / 2;

        // set blit area x and y coordinates, width and height
        rect.x = x;
        rect.y = y;
        rect.w = w;
        rect.h = h;

        // lock screen mutex
        SDL_LockMutex(screen_mutex);

        /* Deprecated, please refer to tutorial04-resampled.c for the new API */
        // Blit the overlay to the display surface specified when the
        // overlay was created.
        SDL_DisplayYUVOverlay(vp->bmp, &rect);

        // unlock screen mutex
        SDL_UnlockMutex(screen_mutex);
    }
}

/**
 * Initialize the given PacketQueue.
 *
 * @param q the PacketQueue to be initialized.
 */
void packet_queue_init(PacketQueue * q)
{
    // alloc memory for the audio queue
    memset(
            q,
            0,
            sizeof(PacketQueue)
    );

    // Returns the initialized and unlocked mutex or NULL on failure
    q->mutex = SDL_CreateMutex();
    if (!q->mutex)
    {
        // could not create mutex
        printf("SDL_CreateMutex Error: %s.\n", SDL_GetError());
        return;
    }

    // Returns a new condition variable or NULL on failure
    q->cond = SDL_CreateCond();
    if (!q->cond)
    {
        // could not create condition variable
        printf("SDL_CreateCond Error: %s.\n", SDL_GetError());
        return;
    }
}

/**
 * Put the given AVPacket in the given PacketQueue.
 *
 * @param  queue    the queue to be used for the insert
 * @param  packet   the AVPacket to be inserted in the queue
 *
 * @return          0 if the AVPacket is correctly inserted in the given PacketQueue.
 */
int packet_queue_put(PacketQueue * queue, AVPacket * packet)
{
    // The packet memory allocation stuff is broken. The packet is allocated if
    // it was not really allocated.
    if (av_dup_packet(packet) < 0)
    {
        return -1;
    }

    // alloc the new AVPacketList to be inserted in the audio PacketQueue
    AVPacketList * avPacketList;
    avPacketList = av_malloc(sizeof(AVPacketList));

    // check the AVPacketList was allocated
    if (!avPacketList)
    {
        return -1;
    }

    // add reference to the given AVPacket
    avPacketList->pkt = * packet;

    // the new AVPacketList will be inserted at the end of the queue
    avPacketList->next = NULL;

    // lock mutex
    SDL_LockMutex(queue->mutex);

    // check the queue is empty
    if (!queue->last_pkt)
    {
        // if it is, insert as first
        queue->first_pkt = avPacketList;
    }
    else
    {
        // if not, insert as last
        queue->last_pkt->next = avPacketList;
    }

    // point the last AVPacketList in the queue to the newly created AVPacketList
    queue->last_pkt = avPacketList;

    // increase by 1 the number of AVPackets in the queue
    queue->nb_packets++;

    // increase queue size by adding the size of the newly inserted AVPacket
    queue->size += avPacketList->pkt.size;

    // notify packet_queue_get which is waiting that a new packet is available
    SDL_CondSignal(queue->cond);

    // unlock mutex
    SDL_UnlockMutex(queue->mutex);

    return 0;
}

/**
 * Get the first AVPacket from the given PacketQueue.
 *
 * @param  queue      The PacketQueue to extract from
 * @param  packet    The first AVPacket extracted from the queue
 * @param  blocking  0 to avoid waiting for an AVPacket to be inserted in the given
 *                queue, != 0 otherwise.
 *
 * @return        < 0 if returning because the quit flag is set, 0 if the queue
 *                is empty, 1 if it is not empty and a packet was extract (pkt)
 */
static int packet_queue_get(PacketQueue * queue, AVPacket * packet, int blocking)
{
    int ret;

    AVPacketList * avPacketList;

    // lock mutex
    SDL_LockMutex(queue->mutex);

    for (;;)
    {
        // check quit flag
        if (global_video_state->quit)
        {
            ret = -1;
            break;
        }

        // point to the first AVPacketList in the queue
        avPacketList = queue->first_pkt;

        // if the first packet is not NULL, the queue is not empty
        if (avPacketList)
        {
            // place the second packet in the queue at first position
            queue->first_pkt = avPacketList->next;

            // check if queue is empty after removal
            if (!queue->first_pkt)
            {
                // first_pkt = last_pkt = NULL = empty queue
                queue->last_pkt = NULL;
            }

            // decrease the number of packets in the queue
            queue->nb_packets--;

            // decrease the size of the packets in the queue
            queue->size -= avPacketList->pkt.size;

            // point packet to the extracted packet, this will return to the calling function
            *packet = avPacketList->pkt;

            // free memory
            av_free(avPacketList);

            ret = 1;
            break;
        }
        else if (!blocking)
        {
            ret = 0;
            break;
        }
        else
        {
            // unlock mutex and wait for cond signal, then lock mutex again
            SDL_CondWait(queue->cond, queue->mutex);
        }
    }

    // unlock mutex
    SDL_UnlockMutex(queue->mutex);

    return ret;
}

/**
 * Pull in data from audio_decode_frame(), store the result in an intermediary
 * buffer, attempt to write as many bytes as the amount defined by len to
 * stream, and get more data if we don't have enough yet, or save it for later
 * if we have some left over.
 *
 * @param userdata  the pointer we gave to SDL.
 * @param stream    the buffer we will be writing audio data to.
 * @param len       the size of that buffer.
 */
void audio_callback(void * userdata, Uint8 * stream, int len)
{
    // retrieve the VideoState
    VideoState * is = (VideoState *)userdata;

    int len1 = -1;
    int audio_size = -1;

    while (len > 0)
    {
        // check global quit flag
        if (global_video_state->quit)
        {
            return;
        }

        if (is->audio_buf_index >= is->audio_buf_size)
        {
            // we have already sent all avaialble data; get more
            audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf));

            // if error
            if (audio_size < 0)
            {
                // output silence
                is->audio_buf_size = 1024;

                // clear memory
                memset(is->audio_buf, 0, is->audio_buf_size);
                printf("audio_decode_frame() failed.\n");
            }
            else
            {
                is->audio_buf_size = audio_size;
            }

            is->audio_buf_index = 0;
        }

        len1 = is->audio_buf_size - is->audio_buf_index;

        if (len1 > len)
        {
            len1 = len;
        }

        // copy data from audio buffer to the SDL stream
        memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);

        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
}

/**
 * Get a packet from the queue if available. Decode the extracted packet. Once
 * we have the frame, resample it and simply copy it to our audio buffer, making
 * sure the data_size is smaller than our audio buffer.
 *
 * @param  videoState   the global VideoState
 * @param  audio_buf    the audio buffer to write into
 * @param  buf_size     the size of the audio buffer, 1.5 larger than the one
 *                      provided by FFmpeg
 *
 * @return              0 if everything goes well, -1 in case of error or quit
 */
int audio_decode_frame(VideoState * videoState, uint8_t *audio_buf, int buf_size)
{
    int len1 = 0;
    int data_size = 0;
    AVPacket * pkt = &videoState->audio_pkt;

    for(;;)
    {
        while(videoState->audio_pkt_size > 0)
        {
            int got_frame = 0;

            /* Deprecated, please refer to tutorial04-resampled.c for the new API */
            len1 = avcodec_decode_audio4(videoState->audio_ctx, &videoState->audio_frame, &got_frame, pkt);

            if (len1 < 0)
            {
                /* if error, skip frame */
                videoState->audio_pkt_size = 0;
                break;
            }

            data_size = 0;

            if (got_frame)
            {
                /* Resampling needed, please refer to tutorial04-resampled.c */
                data_size = av_samples_get_buffer_size(NULL,
                                                       videoState->audio_ctx->channels,
                                                       videoState->audio_frame.nb_samples,
                                                       videoState->audio_ctx->sample_fmt,
                                                       1);
                assert(data_size <= buf_size);

                // copy to the global VideoState buffer
                memcpy(audio_buf, videoState->audio_frame.data[0], data_size);
            }

            videoState->audio_pkt_data += len1;
            videoState->audio_pkt_size -= len1;

            if (data_size <= 0)
            {
                /* No data yet, get more frames */
                continue;
            }

            /* We have data, return it and come back for more later */
            return data_size;
        }

        if (pkt->data)
        {
            av_free_packet(pkt);
        }

        // check global quit flag
        if (videoState->quit)
        {
            return -1;
        }

        /* next packet */
        if (packet_queue_get(&videoState->audioq, pkt, 1) < 0)
        {
            return -1;
        }

        videoState->audio_pkt_data = pkt->data;
        videoState->audio_pkt_size = pkt->size;
    }
}

// [1]
/**
 * SDL_UserEvent -- A user-defined event type.
 *
 * SDL_UserEvent is in the user member of the structure SDL_Event. This event is
 * unique, it is never created by SDL but only by the user. The event can be pushed
 * onto the event queue using SDL_PushEvent. The contents of the structure members
 * or completely up to the programmer, the only requirement is that type is a value
 * from SDL_USEREVENT to SDL_NUMEVENTS-1 (inclusive).
 */

// [2]
/**
 * Allocate a block of size bytes with alignment suitable for all memory accesses
 * (including vectors if available on the CPU) and zero all the bytes of the block.
 * Returns a Pointer to the allocated block, NULL if it cannot be allocated.
 */

// [3]
/**
 * Copy the string src to dst, but no more than size - 1 bytes, and null-terminate
 * dst.
 * This function is the same as BSD strlcpy().
 *
 * Warning: since the return value is the length of src, src absolutely must be a
 * properly 0-terminated string, otherwise this will read beyond the end of the
 * buffer and possibly crash.
 */

// [4]
/**
 * SDL_CreateThread() creates a new thread of execution that shares all of its
 * parent's global memory, signal handlers, file descriptors, etc, and runs the
 * function fn, passing it the void pointer data. The thread quits when fn returns.
 *
 * https://wiki.libsdl.org/SDL_CreateThread
 */

// [5]
/**
 * The event queue can actually be used as a two way communication channel. Not
 * only can events be read from the queue, but the user can also push their own
 * events onto it. event is a pointer to the event structure you wish to push onto
 * the queue. The event is copied into the queue, and the caller may dispose of
 * the memory pointed to after SDL_PushEvent() returns.
 *
 * Note: Pushing device input events onto the queue doesn't modify the state of
 * the device within SDL.
 *
 * This function is thread-safe, and can be called from other threads safely.
 *
 * Note: Events pushed onto the queue with SDL_PushEvent() get passed through the
 * event filter but events added with SDL_PeepEvents() do not.
 *
 * For pushing application-specific events, please use SDL_RegisterEvents() to get
 * an event type that does not conflict with other code that also wants its own
 * custom event types.
 */

// [6]
/**
 * Frees an overlay created by SDL_CreateYUVOverlay.
 *
 * https://www.libsdl.org/release/SDL-1.2.15/docs/html/sdlfreeyuvoverlay.html
 */

// [7]
/**
 * If you use this function, you must pass SDL_INIT_TIMER to SDL_Init().
 *
 * The callback function is passed the current timer interval and the user supplied
 * parameter from the SDL_AddTimer() call and returns the next timer interval. If
 * the returned value from the callback is 0, the timer is canceled.
 *
 * The callback is run on a separate thread. See the code examples for a method
 * of processing the timer callbacks on the main thread if that's desired.
 *
 * https://wiki.libsdl.org/SDL_AddTimer
 */
