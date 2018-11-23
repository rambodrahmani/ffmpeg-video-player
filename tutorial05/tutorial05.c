/**
*
*   File:   tutorial05.c
*           This tutorial adds video to audio synching to the player coded in tutorial04-resampled.c
*
*           Compiled using
*               gcc -o tutorial05 tutorial05.c -lavutil -lavformat -lavcodec -lswscale -lswresample -lz -lm  `sdl2-config --cflags --libs`
*           on Arch Linux.
*
*           Please refer to previous tutorials for uncommented lines of code.
*
*   Author: Rambod Rahmani <rambodrahmani@autistici.org>
*           Created on 8/22/18.
*
**/

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <time.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/avstring.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

/**
 * Prevents SDL from overriding main().
 */
#ifdef __MINGW32__
#undef main
#endif

/**
 * SDL audio buffer size in samples.
 */
#define SDL_AUDIO_BUFFER_SIZE 1024

/**
 * Maximum number of samples per channel in an audio frame.
 */
#define MAX_AUDIO_FRAME_SIZE 192000

/**
 * Audio packets queue maximum size.
 */
#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)

/**
 * Video packets queue maximum size.
 */
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

/**
 * AV sync correction is done if the clock difference is above the maximum AV sync threshold.
 */
#define AV_SYNC_THRESHOLD 0.01

/**
 * No AV sync correction is done if the clock difference is below the minimum AV sync threshold.
 */
#define AV_NOSYNC_THRESHOLD 1.0

/**
 * Custom SDL_Event type.
 * Notifies the next video frame has to be displayed.
 */
#define FF_REFRESH_EVENT (SDL_USEREVENT)

/**
 * Custom SDL_Event type.
 * Notifies the program needs to quit.
 */
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

/**
 * Video Frame queue size.
 */
#define VIDEO_PICTURE_QUEUE_SIZE 1

/**
 * Queue structure used to store AVPackets.
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
 * Queue structure used to store processed video frames.
 *
 * The only thing that changes about queue_picture is that we save that pts value
 * to the VideoPicture structure that we queue up. So we have to add a pts
 * variable to the struct and add a line of code:
 */
typedef struct VideoPicture
{
    AVFrame *   frame;
    int         width;
    int         height;
    int         allocated;
    double      pts;
} VideoPicture;

/**
 * Struct used to hold the format context, the indices of the audio and video stream,
 * the corresponding AVStream objects, the audio and video codec information,
 * the audio and video queues and buffers, the global quit flag and the filename of
 * the movie.
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
    double              audio_clock;
    int                 audio_hw_buf_size;

    /**
     * Video Stream.
     */
    int                 videoStream;
    AVStream *          video_st;
    AVCodecContext *    video_ctx;
    SDL_Texture *       texture;
    SDL_Renderer *      renderer;
    PacketQueue         videoq;
    struct swsContext * sws_ctx;
    double  frame_timer;
    double  frame_last_pts;
    double  frame_last_delay;
    double  video_clock;

    /**
     * VideoPicture Queue.
     */
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

    /**
     * Maximum number of frames to be decoded.
     */
    long    maxFramesToDecode;
    int     currentFrameIndex;
} VideoState;

/**
 * Struct used to hold data fields used for audio resampling.
 */
typedef struct AudioResamplingState
{
    SwrContext * swr_ctx;
    int64_t in_channel_layout;
    uint64_t out_channel_layout;
    int out_nb_channels;
    int out_linesize;
    int in_nb_samples;
    int64_t out_nb_samples;
    int64_t max_out_nb_samples;
    uint8_t ** resampled_data;
    int resampled_data_size;

} AudioResamplingState;

/**
 * Global SDL_Window reference.
 */
SDL_Window * screen;

/**
 * Global SDL_Surface mutex reference.
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
        VideoState * videoState,
        AVFrame * pFrame,
        double pts
);

int video_thread(void * arg);

static int64_t guess_correct_pts(
                    AVCodecContext * ctx,
                    int64_t reordered_pts,
                    int64_t dts
                );

double synchronize_video(
        VideoState * videoState,
        AVFrame * src_frame,
        double pts
);

void video_refresh_timer(void * userdata);

double get_audio_clock(VideoState * videoState);

static void schedule_refresh(
        VideoState * videoState,
        int delay
);

static Uint32 sdl_refresh_timer_cb(
        Uint32 interval,
        void * param
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
        int buf_size,
        double * pts_ptr
);

static int audio_resampling(
        VideoState * videoState,
        AVFrame * decoded_audio_frame,
        enum AVSampleFormat out_sample_fmt,
        uint8_t * out_buf
);

AudioResamplingState * getAudioResampling(uint64_t channel_layout);

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
    // if the given number of command line arguments is wrong
    if ( !(argc == 3) )
    {
        // print help menu and exit
        printHelpMenu();
        return -1;
    }

    int ret = -1;

    /**
     * Initialize SDL.
     * New API: this implementation does not use deprecated SDL functionalities.
     */
    ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
    if (ret != 0)
    {
        printf("Could not initialize SDL - %s\n.", SDL_GetError());
        return -1;
    }

    // the global VideoState reference will be set in decode_thread() to this pointer
    VideoState * videoState = NULL;

    // allocate memory for the VideoState and zero it out
    videoState = av_mallocz(sizeof(VideoState));

    // copy the file name input by the user to the VideoState structure
    av_strlcpy(videoState->filename, argv[1], sizeof(videoState->filename));

    // parse max frames to decode input by the user
    char * pEnd;
    videoState->maxFramesToDecode = strtol(argv[2], &pEnd, 10);

    // initialize locks for the display buffer (pictq)
    videoState->pictq_mutex = SDL_CreateMutex();
    videoState->pictq_cond = SDL_CreateCond();

    // launch our threads by pushing an SDL_event of type FF_REFRESH_EVENT
    schedule_refresh(videoState, 100);

    // start the decoding thread to read data from the AVFormatContext
    videoState->decode_tid = SDL_CreateThread(decode_thread, "Decoding Thread", videoState);

    // check the decode thread was correctly started
    if(!videoState->decode_tid)
    {
        printf("Could not start decoding SDL_Thread: %s.\n", SDL_GetError());

        // free allocated memory before exiting
        av_free(videoState);

        return -1;
    }

    // infinite loop waiting for fired events
    SDL_Event event;
    for(;;)
    {
        // wait indefinitely for the next available event
        ret = SDL_WaitEvent(&event);
        if (ret == 0)
        {
            printf("SDL_WaitEvent failed: %s.\n", SDL_GetError());
        }

        // switch on the retrieved event type
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

    // clean up memory
    av_free(videoState);

    return 0;
}

/**
 * Print help menu containing usage information.
 */
void printHelpMenu()
{
    printf("Invalid arguments.\n\n");
    printf("Usage: ./tutorial05 <filename> <max-frames-to-decode>\n\n");
    printf("e.g: ./tutorial05 /home/rambodrahmani/Videos/video.mp4 200\n");
}

/**
 * This function is used as callback for the SDL_Thread.
 *
 * Opens Audio and Video Streams. If all codecs are retrieved correctly, starts
 * an infinite loop to read AVPackets from the global VideoState AVFormatContext.
 * Based on their stream index, each packet is placed in the appropriate queue.
 *
 * @param   arg the data pointer passed to the SDL_Thread callback function.
 *
 * @return      < 0 in case of error, 0 otherwise.
 */
int decode_thread(void * arg)
{
    // retrieve global VideoState reference
    VideoState * videoState = (VideoState *)arg;

    int ret = -1;

    // file I/O context: demuxers read a media file and split it into chunks of data (packets)
    AVFormatContext * pFormatCtx = NULL;
    ret = avformat_open_input(&pFormatCtx, videoState->filename, NULL, NULL);
    if (ret < 0)
    {
        printf("Could not open file %s.\n", videoState->filename);
        return -1;
    }

    // reset stream indexes
    videoState->videoStream = -1;
    videoState->audioStream = -1;

    // set global VideoState reference
    global_video_state = videoState;

    // set the AVFormatContext for the global VideoState reference
    videoState->pFormatCtx = pFormatCtx;

    // read packets of the media file to get stream information
    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if (ret < 0)
    {
        printf("Could not find stream information: %s.\n", videoState->filename);
        return -1;
    }

    // dump information about file onto standard error
    av_dump_format(pFormatCtx, 0, videoState->filename, 0);

    // video and audio stream indexes
    int videoStream = -1;
    int audioStream = -1;

    // loop through the streams that have been found
    for (int i = 0; i < pFormatCtx->nb_streams; i++)
    {
        // look for the video stream
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0)
        {
            videoStream = i;
        }

        // look for the audio stream
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStream < 0)
        {
            audioStream = i;
        }
    }

    // return with error in case no video stream was found
    if (videoStream == -1)
    {
        printf("Could not find video stream.\n");
        goto fail;
    }
    else
    {
        // open video stream component codec
        ret = stream_component_open(videoState, videoStream);

        // check video codec was opened correctly
        if (ret < 0)
        {
            printf("Could not open video codec.\n");
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
        // open audio stream component codec
        ret = stream_component_open(videoState, audioStream);

        // check audio codec was opened correctly
        if (ret < 0)
        {
            printf("Could not open audio codec.\n");
            goto fail;
        }
    }

    // check both the audio and video codecs were correctly retrieved
    if (videoState->videoStream < 0 || videoState->audioStream < 0)
    {
        printf("Could not open codecs: %s.\n", videoState->filename);
        goto fail;
    }

    // alloc the AVPacket used to read the media file
    AVPacket * packet = av_packet_alloc();
    if (packet == NULL)
    {
        printf("Could not alloc packet.\n");
        return -1;
    }

    // main decode loop: read in a packet and put it on the right queue
    for (;;)
    {
        // check global quit flag
        if (videoState->quit)
        {
            break;
        }

        // check audio and video packets queues size
        if (videoState->audioq.size > MAX_AUDIOQ_SIZE || videoState->videoq.size > MAX_VIDEOQ_SIZE)
        {
            // wait for audio and video queues to decrease size
            SDL_Delay(10);

            continue;
        }

        // read data from the AVFormatContext by repeatedly calling av_read_frame()
        ret = av_read_frame(videoState->pFormatCtx, packet);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                // media EOF reached, quit
                videoState->quit = 1;
                break;
            }
            else if (videoState->pFormatCtx->pb->error == 0)
            {
                // no read error; wait for user input
                SDL_Delay(10);

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
            // otherwise free the memory
            av_packet_unref(packet);
        }
    }

    // wait for the rest of the program to end
    while (!videoState->quit)
    {
        SDL_Delay(100);
    }

    // close the opened input AVFormatContext
    avformat_close_input(&pFormatCtx);  // [5]

    // in case of failure, push the FF_QUIT_EVENT and return
    fail:
    {
        if (1)
        {
            // create an SDL_Event of type FF_QUIT_EVENT
            SDL_Event event;
            event.type = FF_QUIT_EVENT;
            event.user.data1 = videoState;

            // push the event to the events queue
            SDL_PushEvent(&event);

            // return with error
            return -1;
        }
    };

    return 0;
}

/**
 * Retrieves the AVCodec and initializes the AVCodecContext for the given AVStream
 * index. In case of AVMEDIA_TYPE_AUDIO codec type, it sets the desired audio specs,
 * opens the audio device and starts playing.
 *
 * @param   videoState      the global VideoState reference used to save info
 *                          related to the media being played.
 * @param   stream_index    the stream index obtained from the AVFormatContext.
 *
 * @return                  < 0 in case of error, 0 otherwise.
 */
int stream_component_open(VideoState * videoState, int stream_index)
{
    // retrieve file I/O context
    AVFormatContext * pFormatCtx = videoState->pFormatCtx;

    // check the given stream index is valid
    if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams)
    {
        printf("Invalid stream index.");
        return -1;
    }

    // retrieve codec for the given stream index
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

        // check audio device was correctly opened
        if (ret < 0)
        {
            printf("SDL_OpenAudio: %s.\n", SDL_GetError());
            return -1;
        }
    }

    // initialize the AVCodecContext to use the given AVCodec
    if (avcodec_open2(codecCtx, codec, NULL) < 0)
    {
        printf("Unsupported codec.\n");
        return -1;
    }

    // set up the global VideoState based on the type of the codec obtained for
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

            // zero out the block of memory pointed by videoState->audio_pkt
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

            // Don't forget to initialize the frame timer and the initial
            // previous frame delay: 1ms = 1e-6s
            videoState->frame_timer = (double)av_gettime() / 1000000.0;     // [3]
            videoState->frame_last_delay = 40e-3;

            // init video packet queue
            packet_queue_init(&videoState->videoq);

            // start video thread
            videoState->video_tid = SDL_CreateThread(video_thread, "Video Thread", videoState);

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

            // create a window with the specified position, dimensions, and flags.
            screen = SDL_CreateWindow(
                    "FFmpeg SDL Video Player",
                    SDL_WINDOWPOS_UNDEFINED,
                    SDL_WINDOWPOS_UNDEFINED,
                    codecCtx->width/2,
                    codecCtx->height/2,
                    SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI
            );

            // check window was correctly created
            if (!screen)
            {
                printf("SDL: could not create window - exiting.\n");
                return -1;
            }

            //
            SDL_GL_SetSwapInterval(1);

            // initialize global SDL_Surface mutex reference
            screen_mutex = SDL_CreateMutex();

            // create a 2D rendering context for the SDL_Window
            videoState->renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);

            // create a texture for a rendering context
            videoState->texture = SDL_CreateTexture(
                    videoState->renderer,
                    SDL_PIXELFORMAT_YV12,
                    SDL_TEXTUREACCESS_STREAMING,
                    videoState->video_ctx->width,
                    videoState->video_ctx->height
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
 * Allocates a new SDL_Overlay for the VideoPicture struct referenced by the
 * global VideoState struct reference.
 * The remaining VideoPicture struct fields are also updated.
 *
 * @param   userdata    global VideoState reference.
 */
void alloc_picture(void * userdata)
{
    // retrieve global VideoState reference.
    VideoState * videoState = (VideoState *)userdata;

    // retrieve the VideoPicture pointed by the queue write index
    VideoPicture * videoPicture;
    videoPicture = &videoState->pictq[videoState->pictq_windex];

    // check if the SDL_Overlay is allocated
    if (videoPicture->frame)
    {
        // we already have an AVFrame allocated, free memory
        av_frame_free(&videoPicture->frame);
        av_free(videoPicture->frame);
    }

    // lock global screen mutex
    SDL_LockMutex(screen_mutex);

    // get the size in bytes required to store an image with the given parameters
    int numBytes;
    numBytes = av_image_get_buffer_size(
            AV_PIX_FMT_YUV420P,
            videoState->video_ctx->width,
            videoState->video_ctx->height,
            32
    );

    // allocate image data buffer
    uint8_t * buffer = NULL;
    buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));

    // alloc the AVFrame later used to contain the scaled frame
    videoPicture->frame = av_frame_alloc();
    if (videoPicture->frame == NULL)
    {
        printf("Could not allocate frame.\n");
        return;
    }

    // The fields of the given image are filled in by using the buffer which points to the image data buffer.
    av_image_fill_arrays(
            videoPicture->frame->data,
            videoPicture->frame->linesize,
            buffer,
            AV_PIX_FMT_YUV420P,
            videoState->video_ctx->width,
            videoState->video_ctx->height,
            32
    );

    // unlock global screen mutex
    SDL_UnlockMutex(screen_mutex);

    // update VideoPicture struct fields
    videoPicture->width = videoState->video_ctx->width;
    videoPicture->height = videoState->video_ctx->height;
    videoPicture->allocated = 1;
}

/**
 * Waits for space in the VideoPicture queue. Allocates a new SDL_Overlay in case
 * it is not already allocated or has a different width/height. Converts the given
 * decoded AVFrame to an AVPicture using specs supported by SDL and writes it in the
 * VideoPicture queue.
 *
 * @param   videoState  global VideoState reference.
 * @param   pFrame      AVFrame to be inserted in the VideoState->pictq (as an AVPicture).
 *
 * @return              < 0 in case the global quit flag is set, 0 otherwise.
 */
int queue_picture(VideoState * videoState, AVFrame * pFrame, double pts)
{
    // lock VideoState->pictq mutex
    SDL_LockMutex(videoState->pictq_mutex);

    // wait until we have space for a new pic in VideoState->pictq
    while (videoState->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !videoState->quit)
    {
        SDL_CondWait(videoState->pictq_cond, videoState->pictq_mutex);
    }

    // unlock VideoState->pictq mutex
    SDL_UnlockMutex(videoState->pictq_mutex);

    // check global quit flag
    if (videoState->quit)
    {
        return -1;
    }

    // retrieve video picture using the queue write index
    VideoPicture * videoPicture;
    videoPicture = &videoState->pictq[videoState->pictq_windex];

    // if the VideoPicture SDL_Overlay is not allocated or has a different width/height
    if (!videoPicture->frame ||
        videoPicture->width != videoState->video_ctx->width ||
        videoPicture->height != videoState->video_ctx->height)
    {
        // set SDL_Overlay not allocated
        videoPicture->allocated = 0;

        // allocate a new SDL_Overlay for the VideoPicture struct
        alloc_picture(videoState);

        // check global quit flag
        if(videoState->quit)
        {
            return -1;
        }
    }

    // check the new SDL_Overlay was correctly allocated
    if (videoPicture->frame)
    {
        /**
         * So now we've got pictures lining up onto our picture queue with proper
         * PTS values.
         */
        videoPicture->pts = pts;

        // set VideoPicture AVFrame info using the last decoded frame
        videoPicture->frame->pict_type = pFrame->pict_type;
        videoPicture->frame->pts = pFrame->pts;
        videoPicture->frame->pkt_dts = pFrame->pkt_dts;
        videoPicture->frame->key_frame = pFrame->key_frame;
        videoPicture->frame->coded_picture_number = pFrame->coded_picture_number;
        videoPicture->frame->display_picture_number = pFrame->display_picture_number;
        videoPicture->frame->width = pFrame->width;
        videoPicture->frame->height = pFrame->height;

        // scale the image in pFrame->data and put the resulting scaled image in pict->data
        sws_scale(
                videoState->sws_ctx,
                (uint8_t const * const *)pFrame->data,
                pFrame->linesize,
                0,
                videoState->video_ctx->height,
                videoPicture->frame->data,
                videoPicture->frame->linesize
        );

        // update VideoPicture queue write index
        ++videoState->pictq_windex;

        // if the write index has reached the VideoPicture queue size
        if(videoState->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE)
        {
            // set it to 0
            videoState->pictq_windex = 0;
        }

        // lock VideoPicture queue
        SDL_LockMutex(videoState->pictq_mutex);

        // increase VideoPicture queue size
        videoState->pictq_size++;

        // unlock VideoPicture queue
        SDL_UnlockMutex(videoState->pictq_mutex);
    }

    return 0;
}

/**
 * This function is used as callback for the SDL_Thread.
 *
 * This thread reads in packets from the video queue, packet_queue_get(), decodes
 * the video packets into a frame, and then calls the queue_picture() function to
 * put the processed frame into the picture queue.
 *
 * @param   arg the data pointer passed to the SDL_Thread callback function.
 *
 * @return
 */
int video_thread(void * arg)
{
    // retrieve global VideoState reference
    VideoState * videoState = (VideoState *)arg;

    // allocate an AVPacket to be used to retrieve data from the videoq.
    AVPacket * packet = av_packet_alloc();
    if (packet == NULL)
    {
        printf("Could not alloc packet.\n");
        return -1;
    }

    // set this when we are done decoding an entire frame
    int frameFinished;

    // allocate a new AVFrame, used to decode video packets
    static AVFrame * pFrame = NULL;
    pFrame = av_frame_alloc();
    if (!pFrame)
    {
        printf("Could not allocate AVFrame.\n");
        return -1;
    }

    // each decoded frame carries its PTS in the VideoPicture queue
    double pts;

    for (;;)
    {
        // get a packet from the video PacketQueue
        int ret = packet_queue_get(&videoState->videoq, packet, 1);
        if (ret < 0)
        {
            // means we quit getting packets
            break;
        }

        // initially set pts to 0 for all frames
        pts = 0;

        // give the decoder raw compressed data in an AVPacket
        ret = avcodec_send_packet(videoState->video_ctx, packet);
        if (ret < 0)
        {
            printf("Error sending packet for decoding.\n");
            return -1;
        }

        while (ret >= 0)
        {
            // get decoded output data from decoder
            ret = avcodec_receive_frame(videoState->video_ctx, pFrame);

            // check an entire frame was decoded
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret < 0)
            {
                printf("Error while decoding.\n");
                return -1;
            }
            else
            {
                frameFinished = 1;
            }

            // attempt to guess proper monotonic timestamps for decoded video frames
            pts = guess_correct_pts(videoState->video_ctx, pFrame->pts, pFrame->pkt_dts);   // [1]

            // in case we get an undefined timestamp value
            if (pts == AV_NOPTS_VALUE)
            {
                // set pts to the default value of 0
                pts = 0;
            }

            pts *= av_q2d(videoState->video_st->time_base);     // [4]

            // did we get an entire video frame?
            if (frameFinished)
            {
                pts = synchronize_video(videoState, pFrame, pts);

                if(queue_picture(videoState, pFrame, pts) < 0)
                {
                    break;
                }
            }
        }

        // wipe the packet
        av_packet_unref(packet);
    }

    // wipe the frame
    av_frame_free(&pFrame);
    av_free(pFrame);

    return 0;
}

/**
 * Attempts to guess proper monotonic timestamps for decoded video frames which
 * might have incorrect times.
 *
 * Input timestamps may wrap around, in which case the output will as well.
 *
 * @param   ctx             the video AVCodecContext.
 * @param   reordered_pts   the pts field of the decoded AVPacket, as passed
 *                          through AVFrame.pts.
 * @param   dts             the pkt_dts field of the decoded AVPacket.
 *
 * @return                  one of the input values, may be AV_NOPTS_VALUE.
 */
static int64_t guess_correct_pts(AVCodecContext * ctx, int64_t reordered_pts, int64_t dts)
{
    int64_t pts = AV_NOPTS_VALUE;

    if (dts != AV_NOPTS_VALUE)
    {
        ctx->pts_correction_num_faulty_dts += dts <= ctx->pts_correction_last_dts;
        ctx->pts_correction_last_dts = dts;
    }
    else if (reordered_pts != AV_NOPTS_VALUE)
    {
        ctx->pts_correction_last_dts = reordered_pts;
    }

    if (reordered_pts != AV_NOPTS_VALUE)
    {
        ctx->pts_correction_num_faulty_pts += reordered_pts <= ctx->pts_correction_last_pts;
        ctx->pts_correction_last_pts = reordered_pts;
    }
    else if (dts != AV_NOPTS_VALUE)
    {
        ctx->pts_correction_last_pts = dts;
    }

    if ((ctx->pts_correction_num_faulty_pts <= ctx->pts_correction_num_faulty_dts || dts == AV_NOPTS_VALUE) && reordered_pts != AV_NOPTS_VALUE)
    {
        pts = reordered_pts;
    }
    else
    {
        pts = dts;
    }

    return pts;
}

/**
 * So now we've got our PTS all set. Now we've got to take care of the two
 * synchronization problems we talked about above. We're going to define a function
 * called synchronize_video that will update the PTS to be in sync with everything.
 * This function will also finally deal with cases where we don't get a PTS value
 * for our frame. At the same time we need to keep track of when the next frame
 * is expected so we can set our refresh rate properly. We can accomplish this by
 * using an internal video_clock value which keeps track of how much time has
 * passed according to the video. We add this value to our big struct.
 *
 * You'll notice we account for repeated frames in this function, too.
 *
 * @param   videoState
 * @param   src_frame
 * @param   pts
 * @return
 */
double synchronize_video(VideoState * videoState, AVFrame * src_frame, double pts)
{
    double frame_delay;

    if (pts != 0)
    {
        // if we have pts, set video clock to it
        videoState->video_clock = pts;
    }
    else
    {
        // if we aren't given a pts, set it to the clock
        pts = videoState->video_clock;
    }

    // update the video clock
    frame_delay = av_q2d(videoState->video_ctx->time_base);

    // if we are repeating a frame, adjust clock accordingly
    frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);

    videoState->video_clock += frame_delay;

    return pts;
}

/**
 * Pulls from the VideoPicture queue when we have something, sets our timer for
 * when the next video frame should be shown, calls the video_display() method to
 * actually show the video on the screen, then decrements the counter on the queue,
 * and decreases its size.
 *
 * @param   userdata    SDL_UserEvent->data1;   User defined data pointer.
 */
void video_refresh_timer(void * userdata)
{
    // retrieve global VideoState reference
    VideoState * videoState = (VideoState *)userdata;

    /* we will later see how to properly use this */
    VideoPicture * videoPicture;

    // used for video frames display delay and audio video sync
    double pts_delay;
    double audio_ref_clock;
    double sync_threshold;
    double real_delay;
    double audio_video_delay;

    // check the video stream was correctly opened
    if (videoState->video_st)
    {
        // check the VideoPicture queue contains decoded frames
        if (videoState->pictq_size == 0)
        {
            schedule_refresh(videoState, 1);
        }
        else
        {
            // get VideoPicture reference using the queue read index
            videoPicture = &videoState->pictq[videoState->pictq_rindex];

            printf("Current Frame PTS:\t\t%f\n", videoPicture->pts);
            printf("Last Frame PTS:\t\t\t%f\n", videoState->frame_last_pts);

            // get last frame pts
            pts_delay = videoPicture->pts - videoState->frame_last_pts;

            printf("PTS Delay:\t\t\t\t%f\n", pts_delay);

            // if the obtained delay is incorrect
            if (pts_delay <= 0 || pts_delay >= 1.0)
            {
                // use the previously calculated delay
                pts_delay = videoState->frame_last_delay;
            }

            printf("Corrected PTS Delay:\t%f\n", pts_delay);

            // save delay information for the next time
            videoState->frame_last_delay = pts_delay;
            videoState->frame_last_pts = videoPicture->pts;

            // update delay to stay in sync with the audio
            audio_ref_clock = get_audio_clock(videoState);

            printf("Audio Ref Clock:\t\t%f\n", audio_ref_clock);

            audio_video_delay = videoPicture->pts - audio_ref_clock;

            printf("Audio Video Delay:\t\t%f\n", audio_video_delay);

            // skip or repeat the frame taking into account the delay
            sync_threshold = (pts_delay > AV_SYNC_THRESHOLD) ? pts_delay : AV_SYNC_THRESHOLD;

            printf("Sync Threshold:\t\t\t%f\n", sync_threshold);

            // check audio video delay absolute value is below sync threshold
            if (fabs(audio_video_delay) < AV_NOSYNC_THRESHOLD)
            {
                if (audio_video_delay <= -sync_threshold)
                {
                    pts_delay = 0;
                }
                else if (audio_video_delay >= sync_threshold)
                {
                    pts_delay = 2 * pts_delay;  // [2]
                }
            }

            printf("Corrected PTS delay:\t%f\n", pts_delay);

            videoState->frame_timer += pts_delay;   // [2]

            // compute the real delay
            real_delay = videoState->frame_timer - (av_gettime() / 1000000.0);

            printf("Real Delay:\t\t\t\t%f\n", real_delay);

            if (real_delay < 0.010)
            {
                real_delay = 0.010;
            }

            printf("Corrected Real Delay:\t%f\n", real_delay);

            schedule_refresh(videoState, (int)(real_delay * 1000 + 0.5));

            printf("Next Scheduled Refresh:\t%f\n\n", (double)(real_delay * 1000 + 0.5));

            // show the frame on the SDL_Surface (the screen)
            video_display(videoState);

            // update read index for the next frame
            if(++videoState->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE)
            {
                videoState->pictq_rindex = 0;
            }

            // lock VideoPicture queue mutex
            SDL_LockMutex(videoState->pictq_mutex);

            // decrease VideoPicture queue size
            videoState->pictq_size--;

            // notify other threads waiting for the VideoPicture queue
            SDL_CondSignal(videoState->pictq_cond);

            // unlock VideoPicture queue mutex
            SDL_UnlockMutex(videoState->pictq_mutex);
        }
    }
    else
    {
        schedule_refresh(videoState, 100);
    }
}

/**
 * Now we can finally implement our get_audio_clock function. It's not as simple
 * as getting the is->audio_clock value, thought. Notice that we set the audio
 * PTS every time we process it, but if you look at the audio_callback function,
 * it takes time to move all the data from our audio packet into our output
 * buffer. That means that the value in our audio clock could be too far ahead.
 * So we have to check how much we have left to write. Here's the complete code:
 *
 * @param   videoState
 *
 * @return
 */
double get_audio_clock(VideoState * videoState)
{

    double pts = videoState->audio_clock;

    int hw_buf_size = videoState->audio_buf_size - videoState->audio_buf_index;

    int bytes_per_sec = 0;

    int n = 2 * videoState->audio_ctx->channels;

    if (videoState->audio_st)
    {
        bytes_per_sec = videoState->audio_ctx->sample_rate * n;
    }

    if (bytes_per_sec)
    {
        pts -= (double) hw_buf_size / bytes_per_sec;
    }

    return pts;
}

/**
 * Schedules video updates - every time we call this function, it will set the
 * timer, which will trigger an event, which will have our main() function in turn
 * call a function that pulls a frame from our picture queue and displays it.
 *
 * @param   videoState  global VideoState reference.
 * @param   delay       the delay, expressed in milliseconds, before displaying
 *                      the next video frame on the screen.
 */
static void schedule_refresh(VideoState * videoState, int delay)
{
    // schedule an SDL timer
    int ret = SDL_AddTimer(delay, sdl_refresh_timer_cb, videoState);

    // check the timer was correctly scheduled
    if (ret == 0)
    {
        printf("Could not schedule refresh callback: %s.\n.", SDL_GetError());
    }
}

/**
 * This is the callback function for the SDL Timer.
 * Pushes an SDL_Event of type FF_REFRESH_EVENT to the events queue.
 *
 * @param   interval    the timer delay in milliseconds.
 * @param   param       user defined data passed to the callback function when
 *                      scheduling the timer. In our case the global VideoState
 *                      reference.
 *
 * @return              if the returned value from the callback is 0, the timer
 *                      is canceled.
 */
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void * param)
{
    // create an SDL_Event of type FF_REFRESH_EVENT
    SDL_Event event;
    event.type = FF_REFRESH_EVENT;
    event.user.data1 = param;

    // push the event to the events queue
    SDL_PushEvent(&event);

    // return 0 to cancel the timer
    return 0;
}

/**
 * Retrieves the video aspect ratio first, which is just the width divided by the
 * height. Then it scales the movie to fit as big as possible in our screen
 * (SDL_Surface). Then it centers the movie, and calls SDL_DisplayYUVOverlay()
 * to update the surface, making sure we use the screen mutex to access it.
 *
 * @param   videoState  the global VideoState reference.
 */
void video_display(VideoState * videoState)
{
    // reference for the next VideoPicture to be displayed
    VideoPicture * videoPicture;

    float aspect_ratio;

    int w, h, x, y;

    // get next VideoPicture to be displayed from the VideoPicture queue
    videoPicture = &videoState->pictq[videoState->pictq_rindex];

    if (videoPicture->frame)
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

        // get the size of a window's client area
        int screen_width;
        int screen_height;
        SDL_GetWindowSize(screen, &screen_width, &screen_height);

        // global SDL_Surface height
        h = screen_height;

        // retrieve width using the calculated aspect ratio and the screen height
        w = ((int) rint(h * aspect_ratio)) & -3;

        // if the new width is bigger than the screen width
        if (w > screen_width)
        {
            // set the width to the screen width
            w = screen_width;

            // recalculate height using the calculated aspect ratio and the screen width
            h = ((int) rint(w / aspect_ratio)) & -3;
        }

        x = (screen_width - w);
        y = (screen_height - h);

        // check the number of frames to decode was not exceeded
        if (++videoState->currentFrameIndex < videoState->maxFramesToDecode)
        {
            // dump information about the frame being rendered
            printf(
                    "Frame %c (%d) pts %d dts %d key_frame %d [coded_picture_number %d, display_picture_number %d, %dx%d]\n",
                    av_get_picture_type_char(videoPicture->frame->pict_type),
                    videoState->video_ctx->frame_number,
                    videoPicture->frame->pts,
                    videoPicture->frame->pkt_dts,
                    videoPicture->frame->key_frame,
                    videoPicture->frame->coded_picture_number,
                    videoPicture->frame->display_picture_number,
                    videoPicture->frame->width,
                    videoPicture->frame->height
            );

            // set blit area x and y coordinates, width and height
            SDL_Rect rect;
            rect.x = x;
            rect.y = y;
            rect.w = 2*w;
            rect.h = 2*h;

            // lock screen mutex
            SDL_LockMutex(screen_mutex);

            // update the texture with the new pixel data
            SDL_UpdateYUVTexture(
                    videoState->texture,
                    &rect,
                    videoPicture->frame->data[0],
                    videoPicture->frame->linesize[0],
                    videoPicture->frame->data[1],
                    videoPicture->frame->linesize[1],
                    videoPicture->frame->data[2],
                    videoPicture->frame->linesize[2]
            );

            // clear the current rendering target with the drawing color
            SDL_RenderClear(videoState->renderer);

            // copy a portion of the texture to the current rendering target
            SDL_RenderCopy(videoState->renderer, videoState->texture, NULL, NULL);

            // update the screen with any rendering performed since the previous call
            SDL_RenderPresent(videoState->renderer);

            // unlock screen mutex
            SDL_UnlockMutex(screen_mutex);
        }
        else
        {
            // create an SDLEvent of type FF_QUIT_EVENT
            SDL_Event event;
            event.type = FF_QUIT_EVENT;
            event.user.data1 = videoState;

            // push the event
            SDL_PushEvent(&event);
        }
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
 * @param   queue       the PacketQueue to extract from.
 * @param   packet      the first AVPacket extracted from the queue.
 * @param   blocking    0 to avoid waiting for an AVPacket to be inserted in the given
 *                      queue, != 0 otherwise.
 *
 * @return              < 0 if returning because the quit flag is set, 0 if the queue
 *                      is empty, 1 if it is not empty and a packet was extracted.
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
    VideoState * videoState = (VideoState *)userdata;

    int len1 = -1;
    unsigned int audio_size = -1;

    double pts;

    while (len > 0)
    {
        // check global quit flag
        if (global_video_state->quit)
        {
            return;
        }

        if (videoState->audio_buf_index >= videoState->audio_buf_size)
        {
            // we have already sent all avaialble data; get more
            audio_size = audio_decode_frame(videoState, videoState->audio_buf, sizeof(videoState->audio_buf), &pts);

            // if error
            if (audio_size < 0)
            {
                // output silence
                videoState->audio_buf_size = 1024;

                // clear memory
                memset(videoState->audio_buf, 0, videoState->audio_buf_size);
                printf("audio_decode_frame() failed.\n");
            }
            else
            {
                videoState->audio_buf_size = audio_size;
            }

            videoState->audio_buf_index = 0;
        }

        len1 = videoState->audio_buf_size - videoState->audio_buf_index;

        if (len1 > len)
        {
            len1 = len;
        }

        // copy data from audio buffer to the SDL stream
        memcpy(stream, (uint8_t *)videoState->audio_buf + videoState->audio_buf_index, len1);

        len -= len1;
        stream += len1;
        videoState->audio_buf_index += len1;
    }
}

/**
 * Get a packet from the queue if available. Decode the extracted packet. Once
 * we have the frame, resample it and simply copy it to our audio buffer, making
 * sure the data_size is smaller than our audio buffer.
 *
 * @param   aCodecCtx   the audio AVCodecContext used for decoding
 * @param   audio_buf   the audio buffer to write into
 * @param   buf_size    the size of the audio buffer, 1.5 larger than the one
 *                      provided by FFmpeg
 * @param   pts_ptr
 *
 * @return              0 if everything goes well, -1 in case of error or quit
 */
int audio_decode_frame(VideoState * videoState, uint8_t * audio_buf, int buf_size, double * pts_ptr)
{
    AVPacket * avPacket = av_packet_alloc();

    static uint8_t * audio_pkt_data = NULL;
    static int audio_pkt_size = 0;

    double pts;
    int n;

    // allocate a new frame, used to decode audio packets
    static AVFrame * avFrame = NULL;
    avFrame = av_frame_alloc();
    if (!avFrame)
    {
        printf("Could not allocate AVFrame.\n");
        return -1;
    }

    int len1 = 0;
    int data_size = 0;

    for (;;)
    {
        // check global quit flag
        if (videoState->quit)
        {
            return -1;
        }

        while (audio_pkt_size > 0)
        {
            int got_frame = 0;

            int ret = avcodec_receive_frame(videoState->audio_ctx, avFrame);
            if (ret == 0)
            {
                got_frame = 1;
            }
            if (ret == AVERROR(EAGAIN))
            {
                ret = 0;
            }
            if (ret == 0)
            {
                ret = avcodec_send_packet(videoState->audio_ctx, avPacket);
            }
            if (ret == AVERROR(EAGAIN))
            {
                ret = 0;
            }
            else if (ret < 0)
            {
                printf("avcodec_receive_frame error");
                return -1;
            }
            else
            {
                len1 = avPacket->size;
            }

            if (len1 < 0)
            {
                // if error, skip frame
                audio_pkt_size = 0;
                break;
            }

            audio_pkt_data += len1;
            audio_pkt_size -= len1;
            data_size = 0;

            if (got_frame)
            {
                // audio resampling
                data_size = audio_resampling(
                        videoState,
                        avFrame,
                        AV_SAMPLE_FMT_S16,
                        audio_buf
                );

                assert(data_size <= buf_size);
            }

            if (data_size <= 0)
            {
                // no data yet, get more frames
                continue;
            }

            /* Keep audio_clock up-to-date */
            /**
             * Now it's time for us to implement the audio clock. We can update the
             * clock time in our audio_decode_frame function, which is where we decode
             * the audio. Now, remember that we don't always process a new packet
             * every time we call this function, so there are two places we have to
             * update the clock at. The first place is where we get the new packet:
             * we simply set the audio clock to the packet's PTS. Then if a packet
             * has multiple frames, we keep time the audio play by counting the number
             * of samples and multiplying them by the given samples-per-second rate.
             * So once we have the packet:
             */
            pts = videoState->audio_clock;
            *pts_ptr = pts;
            n = 2 * videoState->audio_ctx->channels;
            videoState->audio_clock += (double)data_size / (double)(n * videoState->audio_ctx->sample_rate);

            // we have the data, return it and come back for more later
            return data_size;
        }

        if (avPacket->data)
        {
            // wipe the packet
            av_packet_unref(avPacket);
        }

        // get more audio AVPacket
        int ret = packet_queue_get(&videoState->audioq, avPacket, 1);

        // if packet_queue_get returns < 0, the global quit flag was set
        if (ret < 0)
        {
            return -1;
        }

        audio_pkt_data = avPacket->data;
        audio_pkt_size = avPacket->size;

        /* And once more when we are done processing the packet */
        if (avPacket->pts != AV_NOPTS_VALUE)
        {
            videoState->audio_clock = av_q2d(videoState->audio_st->time_base)*avPacket->pts;
        }
    }

    return 0;
}

/**
 * Resamples the audio data retrieved using FFmpeg before playing it.
 *
 * @param   videoState          the global VideoState reference.
 * @param   decoded_audio_frame the decoded audio frame.
 * @param   out_sample_fmt      audio output sample format (e.g. AV_SAMPLE_FMT_S16).
 * @param   out_buf             audio output buffer.
 *
 * @return                      the size of the resampled audio data.
 */
static int audio_resampling(
        VideoState * videoState,
        AVFrame * decoded_audio_frame,
        enum AVSampleFormat out_sample_fmt,
        uint8_t * out_buf
)
{
    // get an instance of the AudioResamplingState struct
    AudioResamplingState * arState = getAudioResampling(videoState->audio_ctx->channel_layout);

    if (!arState->swr_ctx)
    {
        printf("swr_alloc error.\n");
        return -1;
    }

    // get input audio channels
    arState->in_channel_layout = (videoState->audio_ctx->channels ==
                         av_get_channel_layout_nb_channels(videoState->audio_ctx->channel_layout)) ?
                        videoState->audio_ctx->channel_layout :
                        av_get_default_channel_layout(videoState->audio_ctx->channels);

    // check input audio channels correctly retrieved
    if (arState->in_channel_layout <= 0)
    {
        printf("in_channel_layout error.\n");
        return -1;
    }

    // set output audio channels based on the input audio channels
    if (videoState->audio_ctx->channels == 1)
    {
        arState->out_channel_layout = AV_CH_LAYOUT_MONO;
    }
    else if (videoState->audio_ctx->channels == 2)
    {
        arState->out_channel_layout = AV_CH_LAYOUT_STEREO;
    }
    else
    {
        arState->out_channel_layout = AV_CH_LAYOUT_SURROUND;
    }

    // retrieve number of audio samples (per channel)
    arState->in_nb_samples = decoded_audio_frame->nb_samples;
    if (arState->in_nb_samples <= 0)
    {
        printf("in_nb_samples error.\n");
        return -1;
    }

    // Set SwrContext parameters for resampling
    av_opt_set_int(   // 3
            arState->swr_ctx,
            "in_channel_layout",
            arState->in_channel_layout,
            0
    );

    // Set SwrContext parameters for resampling
    av_opt_set_int(
            arState->swr_ctx,
            "in_sample_rate",
            videoState->audio_ctx->sample_rate,
            0
    );

    // Set SwrContext parameters for resampling
    av_opt_set_sample_fmt(
            arState->swr_ctx,
            "in_sample_fmt",
            videoState->audio_ctx->sample_fmt,
            0
    );

    // Set SwrContext parameters for resampling
    av_opt_set_int(
            arState->swr_ctx,
            "out_channel_layout",
            arState->out_channel_layout,
            0
    );

    // Set SwrContext parameters for resampling
    av_opt_set_int(
            arState->swr_ctx,
            "out_sample_rate",
            videoState->audio_ctx->sample_rate,
            0
    );

    // Set SwrContext parameters for resampling
    av_opt_set_sample_fmt(
            arState->swr_ctx,
            "out_sample_fmt",
            out_sample_fmt,
            0
    );

    // initialize SWR context after user parameters have been set
    int ret = swr_init(arState->swr_ctx);;
    if (ret < 0)
    {
        printf("Failed to initialize the resampling context.\n");
        return -1;
    }

    arState->max_out_nb_samples = arState->out_nb_samples = av_rescale_rnd(
                                                                arState->in_nb_samples,
                                                                videoState->audio_ctx->sample_rate,
                                                                videoState->audio_ctx->sample_rate,
                                                                AV_ROUND_UP
                                                            );

    // check rescaling was successful
    if (arState->max_out_nb_samples <= 0)
    {
        printf("av_rescale_rnd error.\n");
        return -1;
    }

    // get number of output audio channels
    arState->out_nb_channels = av_get_channel_layout_nb_channels(arState->out_channel_layout);

    // allocate data pointers array for arState->resampled_data and fill data
    // pointers and linesize accordingly
    ret = av_samples_alloc_array_and_samples(
            &arState->resampled_data,
            &arState->out_linesize,
            arState->out_nb_channels,
            arState->out_nb_samples,
            out_sample_fmt,
            0
    );

    // check memory allocation for the resampled data was successful
    if (ret < 0)
    {
        printf("av_samples_alloc_array_and_samples() error: Could not allocate destination samples.\n");
        return -1;
    }

    // retrieve output samples number taking into account the progressive delay
    arState->out_nb_samples = av_rescale_rnd(
                                    swr_get_delay(arState->swr_ctx, videoState->audio_ctx->sample_rate) + arState->in_nb_samples,
                                    videoState->audio_ctx->sample_rate,
                                    videoState->audio_ctx->sample_rate,
                                    AV_ROUND_UP
                                );

    // check output samples number was correctly rescaled
    if (arState->out_nb_samples <= 0)
    {
        printf("av_rescale_rnd error\n");
        return -1;
    }

    if (arState->out_nb_samples > arState->max_out_nb_samples)
    {
        // free memory block and set pointer to NULL
        av_free(arState->resampled_data[0]);

        // Allocate a samples buffer for out_nb_samples samples
        ret = av_samples_alloc(
                arState->resampled_data,
                &arState->out_linesize,
                arState->out_nb_channels,
                arState->out_nb_samples,
                out_sample_fmt,
                1
        );

        // check samples buffer correctly allocated
        if (ret < 0)
        {
            printf("av_samples_alloc failed.\n");
            return -1;
        }

        arState->max_out_nb_samples = arState->out_nb_samples;
    }

    if (arState->swr_ctx)
    {
        // do the actual audio data resampling
        ret = swr_convert(
                arState->swr_ctx,
                arState->resampled_data,
                arState->out_nb_samples,
                (const uint8_t **) decoded_audio_frame->data,
                decoded_audio_frame->nb_samples
        );

        // check audio conversion was successful
        if (ret < 0)
        {
            printf("swr_convert_error.\n");
            return -1;
        }

        // get the required buffer size for the given audio parameters
        arState->resampled_data_size = av_samples_get_buffer_size(
                &arState->out_linesize,
                arState->out_nb_channels,
                ret,
                out_sample_fmt,
                1
        );

        // check audio buffer size
        if (arState->resampled_data_size < 0)
        {
            printf("av_samples_get_buffer_size error.\n");
            return -1;
        }
    }
    else
    {
        printf("swr_ctx null error.\n");
        return -1;
    }

    // copy the resampled data to the output buffer
    memcpy(out_buf, arState->resampled_data[0], arState->resampled_data_size);

    /*
     * Memory Cleanup.
     */
    if (arState->resampled_data)
    {
        // free memory block and set pointer to NULL
        av_freep(&arState->resampled_data[0]);
    }

    av_freep(&arState->resampled_data);
    arState->resampled_data = NULL;

    if (arState->swr_ctx)
    {
        // free the allocated SwrContext and set the pointer to NULL
        swr_free(&arState->swr_ctx);
    }

    return arState->resampled_data_size;
}

/**
 * Initializes an instance of the AudioResamplingState Struct with the given
 * parameters.
 *
 * @param   channel_layout  the audio codec context channel layout to be used.
 *
 * @return                  the allocated and initialized AudioResamplingState
 *                          struct instance.
 */
AudioResamplingState * getAudioResampling(uint64_t channel_layout)
{
    AudioResamplingState * audioResampling = av_mallocz(sizeof(AudioResamplingState));

    audioResampling->swr_ctx = swr_alloc();
    audioResampling->in_channel_layout = channel_layout;
    audioResampling->out_channel_layout = AV_CH_LAYOUT_STEREO;
    audioResampling->out_nb_channels = 0;
    audioResampling->out_linesize = 0;
    audioResampling->in_nb_samples = 0;
    audioResampling->out_nb_samples = 0;
    audioResampling->max_out_nb_samples = 0;
    audioResampling->resampled_data = NULL;
    audioResampling->resampled_data_size = 0;

    return audioResampling;
}

// [1]
/**
 * Attempt to guess proper monotonic timestamps for decoded video frames which
 * might have incorrect times. Input timestamps may wrap around, in which case
 * the output will as well.
 *
 * First let's look at our video thread. Remember, this is where we pick up the
 * packets that were put on the queue by our decode thread. What we need to do
 * in this part of the code is get the PTS of the frame given to us by
 * avcodec_decode_video2. The first way we talked about was getting the DTS of
 * the last packet processed, which is pretty easy.
 *
 * We set the PTS to 0 if we can't figure out what it is.
 *
 * Well, that was easy. A technical note: You may have noticed we're using int64
 * for the PTS. This is because the PTS is stored as an integer. This value is a
 * timestamp that corresponds to a measurement of time in that stream's time_base
 * unit. For example, if a stream has 24 frames per second, a PTS of 42 is going
 * to indicate that the frame should go where the 42nd frame would be if there we
 * had a frame every 1/24 of a second (certainly not necessarily true).
 *
 * We can convert this value to seconds by dividing by the framerate. The time_base
 * value of the stream is going to be 1/framerate (for fixed-fps content), so to
 * get the PTS in seconds, we multiply by the time_base.
 */

// [2]
/**
 * You may recall from last time that we just faked it and put a refresh of 80ms.
 * Well, now we're going to find out how to actually figure it out.
 *
 * Our strategy is going to be to predict the time of the next PTS by simply measuring
 * the time between the previous pts and this one. At the same time, we need to
 * sync the video to the audio. We're going to make an audio clock: an internal
 * value that keeps track of what position the audio we're playing is at. It's
 * like the digital readout on any mp3 player. Since we're synching the video to
 * the audio, the video thread uses this value to figure out if it's too far
 * ahead or too far behind.
 *
 * We'll get to the implementation later; for now let's assume we have a
 * get_audio_clock function that will give us the time on the audio clock. Once
 * we have that value, though, what do we do if the video and audio are out of
 * sync? It would silly to simply try and leap to the correct packet through
 * seeking or something. Instead, we're just going to adjust the value we've
 * calculated for the next refresh: if the PTS is too far behind the audio time,
 * we double our calculated delay. If the PTS is too far ahead of the audio time,
 * we simply refresh as quickly as possible. Now that we have our adjusted refresh
 * time, or delay, we're going to compare that with our computer's clock by keeping
 * a running frame_timer. This frame timer will sum up all of our calculated delays
 * while playing the movie. In other words, this frame_timer is what time it
 * should be when we display the next frame. We simply add the new delay to the
 * frame timer, compare it to the time on our computer's clock, and use that value
 * to schedule the next refresh. This might be a bit confusing, so study the code
 * carefully.
 *
 * There are a few checks we make: first, we make sure that the delay between the
 * PTS and the previous PTS make sense. If it doesn't we just guess and use the
 * last delay. Next, we make sure we have a synch threshold because things are
 * never going to be perfectly in synch. FFplay uses 0.01 for its value. We also
 * make sure that the synch threshold is never smaller than the gaps in between
 * PTS values. Finally, we make the minimum refresh value 10 milliseconds.
 */

// [3]
/**
 * Get the current time in microseconds.
 */

// [4]
/**
 * This is the fundamental unit of time (in seconds) in terms of which frame
 * timestamps are represented.
 *
 * decoding: set by libavformat
 *
 * encoding: May be set by the caller before avformat_write_header() to provide
 * a hint to the muxer about the desired timebase. In avformat_write_header(),
 * the muxer will overwrite this field with the timebase that will actually be
 * used for the timestamps written into the file (which may or may not be related
 * to the user-provided one, depending on the format).
 */

// [5]
/**
 * Close an opened input AVFormatContext.
 *
 * Free it and all its contents and set *s to NULL.
 */
