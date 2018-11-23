/**
*
*   File:   tutorial03-resampled.c
*           This tutorial adds resampling to the implementation provided in
*           tutorial03-deprecated.c in order to be able to obtain a clear
*           output sound.
*
*           Compiled using
*               gcc -o tutorial03-resampled tutorial03-resampled.c -lavutil -lavformat -lavcodec -lswscale -lswresample -lz -lm  `sdl2-config --cflags --libs`
*           on Arch Linux.
*
*           Please refer to previous tutorials for uncommented lines of code.
*           This implementation uses new APIs. Please refer to
*           tutorial03-resampled-deprecated.c for the implementation using deprecated
*           APIs.
*
*   Author: Rambod Rahmani <rambodrahmani@autistici.org>
*           Created on 8/14/18.
*
**/

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

/* Prevents SDL from overriding main() */
#ifdef __MINGW32__
#undef main
#endif

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

/**
 * PacketQueue Structure Declaration.
 */
typedef struct PacketQueue
{
    AVPacketList * first_pkt;
    AVPacketList * last_pkt;
    int nb_packets;
    int size;
    SDL_mutex * mutex;
    SDL_cond * cond;
} PacketQueue;

// audio PacketQueue instance
PacketQueue audioq;

// global quit flag
int quit = 0;

void printHelpMenu();

void packet_queue_init(PacketQueue * q);

int packet_queue_put(
        PacketQueue * queue,
        AVPacket * packet
    );

static int packet_queue_get(
              PacketQueue * q,
              AVPacket * pkt,
              int block
           );

void audio_callback(
        void * userdata,
        Uint8 * stream,
        int len
     );

int audio_decode_frame(
        AVCodecContext * aCodecCtx,
        uint8_t * audio_buf,
        int buf_size
    );

static int audio_resampling(
              AVCodecContext * audio_decode_ctx,
              AVFrame * audio_decode_frame,
              enum AVSampleFormat out_sample_fmt,
              int out_channels,
              int out_sample_rate,
              uint8_t * out_buf
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
    // if the given number of command line arguments is wrong,
    if ( !(argc == 3) )
    {
        // print help menu and exit
        printHelpMenu();
        return -1;
    }

    // parse max frames to decode input from command line args
    int maxFramesToDecode = -1;
    sscanf(argv[2], "%d", &maxFramesToDecode);

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

    // file I/O context: demuxers read a media file and split it into chunks of data (packets)
    AVFormatContext * pFormatCtx = NULL;
    ret = avformat_open_input(&pFormatCtx, argv[1], NULL, NULL);
    if (ret < 0)
    {
        printf("Could not open file %s.\n", argv[1]);
        return -1;
    }

    // read packets of a media file to get stream information
    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if (ret < 0)
    {
        printf("Could not find stream information %s.\n", argv[1]);
        return -1;
    }

    // print detailed information about the input or output format
    av_dump_format(pFormatCtx, 0, argv[1], 0);

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

    // return with error in case no video stream was found
    if (videoStream == -1)
    {
        printf("Could not find video stream.\n");
        return -1;
    }

    // return with error in case no audio stream was found
    if (audioStream == -1)
    {
        printf("Could not find audio stream.\n");
        return -1;
    }

    // retrieve audio codec
    AVCodec * aCodec = NULL;
    aCodec = avcodec_find_decoder(pFormatCtx->streams[audioStream]->codecpar->codec_id);
    if (aCodec == NULL)
    {
        printf("Unsupported codec!\n");
        return -1;
    }

    // retrieve audio codec context
    AVCodecContext * aCodecCtx = NULL;
    aCodecCtx = avcodec_alloc_context3(aCodec);
    ret = avcodec_parameters_to_context(aCodecCtx, pFormatCtx->streams[audioStream]->codecpar);
    if (ret != 0)
    {
        printf("Could not copy codec context.\n");
        return -1;
    }

    // audio specs containers
    SDL_AudioSpec wanted_specs;
    SDL_AudioSpec specs;

    // set audio settings from codec info
    wanted_specs.freq = aCodecCtx->sample_rate;
    wanted_specs.format = AUDIO_S16SYS;
    wanted_specs.channels = aCodecCtx->channels;
    wanted_specs.silence = 0;
    wanted_specs.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_specs.callback = audio_callback;
    wanted_specs.userdata = aCodecCtx;

    // Uint32 audio device id
    SDL_AudioDeviceID audioDeviceID;

    // open audio device
    audioDeviceID = SDL_OpenAudioDevice(    // [1]
                          NULL,
                          0,
                          &wanted_specs,
                          &specs,
                          SDL_AUDIO_ALLOW_FORMAT_CHANGE
                      );

    // SDL_OpenAudioDevice returns a valid device ID that is > 0 on success or 0 on failure
    if (audioDeviceID == 0)
    {
        printf("Failed to open audio device: %s.\n", SDL_GetError());
        return -1;
    }

    // initialize the audio AVCodecContext to use the given audio AVCodec
    ret = avcodec_open2(aCodecCtx, aCodec, NULL);
    if (ret < 0)
    {
        printf("Could not open audio codec.\n");
        return -1;
    }

    // init audio PacketQueue
    packet_queue_init(&audioq);

    // start playing audio on the given audio device
    SDL_PauseAudioDevice(audioDeviceID, 0);   // [2]

    // retrieve video codec
    AVCodec * pCodec = NULL;
    pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
    if (pCodec == NULL)
    {
        printf("Unsupported codec!\n");
        return -1;
    }

    // retrieve video codec context
    AVCodecContext * pCodecCtx = NULL;
    pCodecCtx = avcodec_alloc_context3(pCodec);
    ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
    if (ret != 0)
    {
        printf("Could not copy codec context.\n");
        return -1;
    }

    // initialize the video AVCodecContext to use the given video AVCodec
    ret = avcodec_open2(pCodecCtx, pCodec, NULL);
    if (ret < 0)
    {
        printf("Could not open codec.\n");
        return -1;
    }

    // alloc AVFrame for video decoding
    AVFrame * pFrame = av_frame_alloc();
    if (pFrame == NULL)
    {
        printf("Could not allocate frame.\n");
        return -1;
    }

    // create a window with the specified position, dimensions, and flags.
    SDL_Window * screen = SDL_CreateWindow(
                              "FFmpeg SDL Video Player",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              pCodecCtx->width/2,
                              pCodecCtx->height/2,
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

    // create a 2D rendering context for the SDL_Window
    SDL_Renderer * renderer = NULL;
    renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);

    // create a texture for a rendering context
    SDL_Texture * texture = NULL;
    texture = SDL_CreateTexture(
                  renderer,
                  SDL_PIXELFORMAT_YV12,
                  SDL_TEXTUREACCESS_STREAMING,
                  pCodecCtx->width,
                  pCodecCtx->height
              );

    // alloc an AVPacket to read data from the AVFormatContext
    AVPacket * pPacket = av_packet_alloc();
    if (pPacket == NULL)
    {
        printf("Could not alloc packet.\n");
        return -1;
    }

    // set up our SWSContext to convert the image data to YUV420
    struct SwsContext * sws_ctx = NULL;
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

    // get the size in bytes required to store an image with the given parameters
    int numBytes;
    numBytes = av_image_get_buffer_size(
                  AV_PIX_FMT_YUV420P,
                  pCodecCtx->width,
                  pCodecCtx->height,
                  32
              );

    // allocate image data buffer
    uint8_t * buffer = NULL;
    buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));

    // alloc the AVFrame later used to contain the scaled frame
    AVFrame * pict = av_frame_alloc();

    // The fields of the given image are filled in by using the buffer which points to the image data buffer.
    av_image_fill_arrays(
        pict->data,
        pict->linesize,
        buffer,
        AV_PIX_FMT_YUV420P,
        pCodecCtx->width,
        pCodecCtx->height,
        32
    );

    // used later to handle quit event
    SDL_Event event;

    // current video frame index
    int frameIndex = 0;

    // read data from the AVFormatContext by repeatedly calling av_read_frame()
    while (av_read_frame(pFormatCtx, pPacket) >= 0)
    {
        // video stream found
        if (pPacket->stream_index == videoStream)
        {
            // give the decoder raw compressed data in an AVPacket
            ret = avcodec_send_packet(pCodecCtx, pPacket);
            if (ret < 0)
            {
                printf("Error sending packet for decoding.\n");
                return -1;
            }

            while (ret >= 0)
            {
                // get decoded output data from decoder
                ret = avcodec_receive_frame(pCodecCtx, pFrame);

                // check an entire frame was decoded
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)   // [3]
                {
                    break;
                }
                else if (ret < 0)
                {
                    printf("Error while decoding.\n");
                    return -1;
                }

                // scale the image in pFrame->data and put the resulting scaled image in pict->data
                sws_scale(
                    sws_ctx,
                    (uint8_t const * const *)pFrame->data,
                    pFrame->linesize,
                    0,
                    pCodecCtx->height,
                    pict->data,
                    pict->linesize
                );

                // check the number of frames to decode was not exceeded
                if (++frameIndex <= maxFramesToDecode)
                {
                    // dump information about the frame being rendered
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

                    // the area of the texture to be updated
                    SDL_Rect rect;
                    rect.x = 0;
                    rect.y = 0;
                    rect.w = pCodecCtx->width;
                    rect.h = pCodecCtx->height;

                    // update the texture with the new pixel data
                    SDL_UpdateYUVTexture(
                        texture,
                        &rect,
                        pict->data[0],
                        pict->linesize[0],
                        pict->data[1],
                        pict->linesize[1],
                        pict->data[2],
                        pict->linesize[2]
                    );

                    // clear the current rendering target with the drawing color
                    SDL_RenderClear(renderer);

                    // copy a portion of the texture to the current rendering target
                    SDL_RenderCopy(renderer, texture, NULL, NULL);

                    // update the screen with any rendering performed since the previous call
                    SDL_RenderPresent(renderer);
                }
            }

            // exit decoding loop if the number of frames to decode was not exceeded
            if (frameIndex > maxFramesToDecode)
            {
                printf("Max number of frames to decode processed. Quitting.\n");
                break;
            }
        }
        // audio stream found
        else if (pPacket->stream_index == audioStream)
        {
            // put the AVPacket in the audio PacketQueue
            packet_queue_put(&audioq, pPacket);
        }
        // everything else
        else
        {
            // wipe the packet
            av_packet_unref(pPacket);
        }

        // handle quit event (Ctrl + C, SDL Window closed)
        SDL_PollEvent(&event);
        switch(event.type)
        {
            case SDL_QUIT:
            {
                printf("SDL_QUIT event received. Quitting.\n");

                // clean up all SDL initialized subsystems
                SDL_Quit();

                // set global quit flag
                quit = 1;
            }
            break;

            default:
            {
                // nothing to do
            }
            break;
        }

        // exit decoding loop if global quit flag is set
        if (quit)
        {
          break;
        }
    }

    /**
    * Memory Cleanup.
    */
    av_packet_unref(pPacket);

    av_frame_free(&pict);
    av_free(pict);

    av_frame_free(&pFrame);
    av_free(pFrame);

    avcodec_close(pCodecCtx);
    avcodec_close(aCodecCtx);

    avformat_close_input(&pFormatCtx);

    return 0;
}

/**
 * Print help menu containing usage information.
 */
void printHelpMenu()
{
    printf("Invalid arguments.\n\n");
    printf("Usage: ./tutorial03-resampled <filename> <max-frames-to-decode>\n\n");
    printf("e.g: ./tutorial03-resampled /home/rambodrahmani/Videos/video.mp4 200\n");
}

/**
 * Initialize the given PacketQueue.
 *
 * @param   q   the PacketQueue to be initialized.
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
 * @param   q   the queue to be used for the insert
 * @param   pkt the AVPacket to be inserted in the queue
 *
 * @return      0 if the AVPacket is correctly inserted in the given PacketQueue.
 */
int packet_queue_put(PacketQueue * q, AVPacket * pkt)
{
    /* [4]
     * if (av_dup_packet(pkt) < 0) { return -1; }
     */

    // alloc the new AVPacketList to be inserted in the audio PacketQueue
    AVPacketList * avPacketList;
    avPacketList = av_malloc(sizeof(AVPacketList));

    // check the AVPacketList was allocated
    if (!avPacketList)
    {
        return -1;
    }

    // add reference to the given AVPacket
    avPacketList->pkt = * pkt;

    // the new AVPacketList will be inserted at the end of the queue
    avPacketList->next = NULL;

    // lock mutex
    SDL_LockMutex(q->mutex);

    // check the queue is empty
    if (!q->last_pkt)
    {
        // if it is, insert as first
        q->first_pkt = avPacketList;
    }
    else
    {
        // if not, insert as last
        q->last_pkt->next = avPacketList;
    }

    // point the last AVPacketList in the queue to the newly created AVPacketList
    q->last_pkt = avPacketList;

    // increase by 1 the number of AVPackets in the queue
    q->nb_packets++;

    // increase queue size by adding the size of the newly inserted AVPacket
    q->size += avPacketList->pkt.size;

    // notify packet_queue_get which is waiting that a new packet is available
    SDL_CondSignal(q->cond);

    // unlock mutex
    SDL_UnlockMutex(q->mutex);

    return 0;
}

/**
 * Get the first AVPacket from the given PacketQueue.
 *
 * @param   q       the PacketQueue to extract from
 * @param   pkt     the first AVPacket extracted from the queue
 * @param   block   0 to avoid waiting for an AVPacket to be inserted in the given
 *                  queue, != 0 otherwise.
 *
 * @return          < 0 if returning because the quit flag is set, 0 if the queue
 *                  is empty, 1 if it is not empty and a packet was extract (pkt)
 */
static int packet_queue_get(PacketQueue * q, AVPacket * pkt, int block)
{
    int ret;

    AVPacketList * avPacketList;

    // lock mutex
    SDL_LockMutex(q->mutex);

    for (;;)
    {
        // check quit flag
        if (quit)
        {
            ret = -1;
            break;
        }

        // point to the first AVPacketList in the queue
        avPacketList = q->first_pkt;

        // if the first packet is not NULL, the queue is not empty
        if (avPacketList)
        {
            // place the second packet in the queue at first position
            q->first_pkt = avPacketList->next;

            // check if queue is empty after removal
            if (!q->first_pkt)
            {
                // first_pkt = last_pkt = NULL = empty queue
                q->last_pkt = NULL;
            }

            // decrease the number of packets in the queue
            q->nb_packets--;

            // decrease the size of the packets in the queue
            q->size -= avPacketList->pkt.size;

            // point pkt to the extracted packet, this will return to the calling function
            *pkt = avPacketList->pkt;

            // free memory
            av_free(avPacketList);

            ret = 1;
            break;
        }
        else if (!block)
        {
            ret = 0;
            break;
        }
        else
        {
            // unlock mutex and wait for cond signal, then lock mutex again
            SDL_CondWait(q->cond, q->mutex);
        }
    }

    // unlock mutex
    SDL_UnlockMutex(q->mutex);

    return ret;
}

/**
 * Pull in data from audio_decode_frame(), store the result in an intermediary
 * buffer, attempt to write as many bytes as the amount defined by len to
 * stream, and get more data if we don't have enough yet, or save it for later
 * if we have some left over.
 *
 * @param   userdata    the pointer we gave to SDL.
 * @param   stream      the buffer we will be writing audio data to.
 * @param   len         the size of that buffer.
 */
void audio_callback(void * userdata, Uint8 * stream, int len)
{
    // retrieve the audio codec context
    AVCodecContext * aCodecCtx = (AVCodecContext *) userdata;

    int len1 = -1;
    int audio_size = -1;

    // The size of audio_buf is 1.5 times the size of the largest audio frame
    // that FFmpeg will give us, which gives us a nice cushion.
    static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    while (len > 0)
    {
        // check global quit flag
        if (quit)
        {
            return;
        }

        if (audio_buf_index >= audio_buf_size)
        {
            // we have already sent all avaialble data; get more
            audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));

            // if error
            if (audio_size < 0)
            {
                // output silence
                audio_buf_size = 1024;

                // clear memory
                memset(audio_buf, 0, audio_buf_size);
                printf("audio_decode_frame() failed.\n");
            }
            else
            {
                audio_buf_size = audio_size;
            }

            audio_buf_index = 0;
        }

        len1 = audio_buf_size - audio_buf_index;

        if (len1 > len)
        {
            len1 = len;
        }

        // copy data from audio buffer to the SDL stream
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);

        len -= len1;
        stream += len1;
        audio_buf_index += len1;
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
 *
 * @return              0 if everything goes well, -1 in case of error or quit
 */
int audio_decode_frame(AVCodecContext * aCodecCtx, uint8_t * audio_buf, int buf_size)
{
    AVPacket * avPacket = av_packet_alloc();
    static uint8_t * audio_pkt_data = NULL;
    static int audio_pkt_size = 0;

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
        if (quit)
        {
            return -1;
        }

        while (audio_pkt_size > 0)
        {
            int got_frame = 0;

            // [5]
            // len1 = avcodec_decode_audio4(aCodecCtx, avFrame, &got_frame, avPacket);
            int ret = avcodec_receive_frame(aCodecCtx, avFrame);
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
                ret = avcodec_send_packet(aCodecCtx, avPacket);
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
                                aCodecCtx,
                                avFrame,
                                AV_SAMPLE_FMT_S16,
                                aCodecCtx->channels,
                                aCodecCtx->sample_rate,
                                audio_buf
                            );

                assert(data_size <= buf_size);
            }

            if (data_size <= 0)
            {
                // no data yet, get more frames
                continue;
            }

            // we have the data, return it and come back for more later
            return data_size;
        }

        if (avPacket->data)
        {
            // wipe the packet
            av_packet_unref(avPacket);
        }

        // get more audio AVPacket
        int ret = packet_queue_get(&audioq, avPacket, 1);

        // if packet_queue_get returns < 0, the global quit flag was set
        if (ret < 0)
        {
            return -1;
        }

        audio_pkt_data = avPacket->data;
        audio_pkt_size = avPacket->size;
    }

    return 0;
}

/**
 * Resample the audio data retrieved using FFmpeg before playing it.
 *
 * @param   audio_decode_ctx    the audio codec context retrieved from the original AVFormatContext.
 * @param   decoded_audio_frame the decoded audio frame.
 * @param   out_sample_fmt      audio output sample format (e.g. AV_SAMPLE_FMT_S16).
 * @param   out_channels        audio output channels, retrieved from the original audio codec context.
 * @param   out_sample_rate     audio output sample rate, retrieved from the original audio codec context.
 * @param   out_buf             audio output buffer.
 *
 * @return                      the size of the resampled audio data.
 */
static int audio_resampling(                                  // 1
                        AVCodecContext * audio_decode_ctx,
                        AVFrame * decoded_audio_frame,
                        enum AVSampleFormat out_sample_fmt,
                        int out_channels,
                        int out_sample_rate,
                        uint8_t * out_buf
                      )
{
    // check global quit flag
    if (quit)
    {
        return -1;
    }

    SwrContext * swr_ctx = NULL;
    int ret = 0;
    int64_t in_channel_layout = audio_decode_ctx->channel_layout;
    int64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    int out_nb_channels = 0;
    int out_linesize = 0;
    int in_nb_samples = 0;
    int out_nb_samples = 0;
    int max_out_nb_samples = 0;
    uint8_t ** resampled_data = NULL;
    int resampled_data_size = 0;

    swr_ctx = swr_alloc();

    if (!swr_ctx)
    {
        printf("swr_alloc error.\n");
        return -1;
    }

    // get input audio channels
    in_channel_layout = (audio_decode_ctx->channels ==
                     av_get_channel_layout_nb_channels(audio_decode_ctx->channel_layout)) ?   // 2
                     audio_decode_ctx->channel_layout :
                     av_get_default_channel_layout(audio_decode_ctx->channels);

    // check input audio channels correctly retrieved
    if (in_channel_layout <= 0)
    {
        printf("in_channel_layout error.\n");
        return -1;
    }

    // set output audio channels based on the input audio channels
    if (out_channels == 1)
    {
        out_channel_layout = AV_CH_LAYOUT_MONO;
    }
    else if (out_channels == 2)
    {
        out_channel_layout = AV_CH_LAYOUT_STEREO;
    }
    else
    {
        out_channel_layout = AV_CH_LAYOUT_SURROUND;
    }

    // retrieve number of audio samples (per channel)
    in_nb_samples = decoded_audio_frame->nb_samples;
    if (in_nb_samples <= 0)
    {
        printf("in_nb_samples error.\n");
        return -1;
    }

    // Set SwrContext parameters for resampling
    av_opt_set_int(   // 3
        swr_ctx,
        "in_channel_layout",
        in_channel_layout,
        0
    );

    // Set SwrContext parameters for resampling
    av_opt_set_int(
        swr_ctx,
        "in_sample_rate",
        audio_decode_ctx->sample_rate,
        0
    );

    // Set SwrContext parameters for resampling
    av_opt_set_sample_fmt(
        swr_ctx,
        "in_sample_fmt",
        audio_decode_ctx->sample_fmt,
        0
    );

    // Set SwrContext parameters for resampling
    av_opt_set_int(
        swr_ctx,
        "out_channel_layout",
        out_channel_layout,
        0
    );

    // Set SwrContext parameters for resampling
    av_opt_set_int(
        swr_ctx,
        "out_sample_rate",
        out_sample_rate,
        0
    );

    // Set SwrContext parameters for resampling
    av_opt_set_sample_fmt(
        swr_ctx,
        "out_sample_fmt",
        out_sample_fmt,
        0
    );

    // Once all values have been set for the SwrContext, it must be initialized
    // with swr_init().
    ret = swr_init(swr_ctx);;
    if (ret < 0)
    {
        printf("Failed to initialize the resampling context.\n");
        return -1;
    }

    max_out_nb_samples = out_nb_samples = av_rescale_rnd(
                                              in_nb_samples,
                                              out_sample_rate,
                                              audio_decode_ctx->sample_rate,
                                              AV_ROUND_UP
                                          );

    // check rescaling was successful
    if (max_out_nb_samples <= 0)
    {
        printf("av_rescale_rnd error.\n");
        return -1;
    }

    // get number of output audio channels
    out_nb_channels = av_get_channel_layout_nb_channels(out_channel_layout);

    ret = av_samples_alloc_array_and_samples(
              &resampled_data,
              &out_linesize,
              out_nb_channels,
              out_nb_samples,
              out_sample_fmt,
              0
          );

    if (ret < 0)
    {
        printf("av_samples_alloc_array_and_samples() error: Could not allocate destination samples.\n");
        return -1;
    }

    // retrieve output samples number taking into account the progressive delay
    out_nb_samples = av_rescale_rnd(
                        swr_get_delay(swr_ctx, audio_decode_ctx->sample_rate) + in_nb_samples,
                        out_sample_rate,
                        audio_decode_ctx->sample_rate,
                        AV_ROUND_UP
                     );

    // check output samples number was correctly retrieved
    if (out_nb_samples <= 0)
    {
        printf("av_rescale_rnd error\n");
        return -1;
    }

    if (out_nb_samples > max_out_nb_samples)
    {
        // free memory block and set pointer to NULL
        av_free(resampled_data[0]);

        // Allocate a samples buffer for out_nb_samples samples
        ret = av_samples_alloc(
                  resampled_data,
                  &out_linesize,
                  out_nb_channels,
                  out_nb_samples,
                  out_sample_fmt,
                  1
              );

        // check samples buffer correctly allocated
        if (ret < 0)
        {
            printf("av_samples_alloc failed.\n");
            return -1;
        }

        max_out_nb_samples = out_nb_samples;
    }

    if (swr_ctx)
    {
        // do the actual audio data resampling
        ret = swr_convert(
                  swr_ctx,
                  resampled_data,
                  out_nb_samples,
                  (const uint8_t **) decoded_audio_frame->data,
                  decoded_audio_frame->nb_samples
              );

        // check audio conversion was successful
        if (ret < 0)
        {
            printf("swr_convert_error.\n");
            return -1;
        }

        // Get the required buffer size for the given audio parameters
        resampled_data_size = av_samples_get_buffer_size(
                                  &out_linesize,
                                  out_nb_channels,
                                  ret,
                                  out_sample_fmt,
                                  1
                              );

        // check audio buffer size
        if (resampled_data_size < 0)
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
    memcpy(out_buf, resampled_data[0], resampled_data_size);

    /*
     * Memory Cleanup.
     */
    if (resampled_data)
    {
        // free memory block and set pointer to NULL
        av_freep(&resampled_data[0]);
    }

    av_freep(&resampled_data);
    resampled_data = NULL;

    if (swr_ctx)
    {
        // Free the given SwrContext and set the pointer to NULL
        swr_free(&swr_ctx);
    }

    return resampled_data_size;
}

// [1]
/**
 * Use this function to open a specific audio device.
 *
 * Returns a valid device ID that is > 0 on success or 0 on failure; call
 * SDL_GetError() for more information.
 *
 * SDL_OpenAudio(), unlike this function, always acts on device ID 1. As such,
 * this function will never return a 1 so as not to conflict with the legacy
 * function.
 *
 * Function Parameters:
 *    device:           a UTF-8 string reported by SDL_GetAudioDeviceName(); Passing in
 *                      a device name of NULL requests the most reasonable default (and
 *                      is equivalent to what SDL_OpenAudio() does to choose a device).
 *                      The device name is a UTF-8 string reported by SDL_GetAudioDeviceName(),
 *                      but some drivers allow arbitrary and driver-specific strings, such as
 *                      a hostname/IP address for a remote audio server, or a filename
 *                      in the diskaudio driver.
 *    iscapture:        non-zero to specify a device should be opened for recording,
 *                      not playback
 *    desired:          an SDL_AudioSpec structure representing the desired output format;
 *                      see SDL_OpenAudio() for more information
 *    obtained:         an SDL_AudioSpec structure filled in with the actual output format;
 *                      see SDL_OpenAudio() for more information
 *    allowed_changes:  0, or one or more flags OR'd together; These flags specify how SDL
 *                      should behave when a device cannot offer a specific feature. If
 *                      the application requests a feature that the hardware doesn't offer,
 *                      SDL will always try to get the closest equivalent. For example, if
 *                      you ask for float32 audio format, but the sound card only supports
 *                      int16, SDL will set the hardware to int16. If you had set
 *                      SDL_AUDIO_ALLOW_FORMAT_CHANGE, SDL will change the format in the
 *                      obtained structure. If that flag was not set, SDL will prepare to
 *                      convert your callback's float32 audio to int16 before feeding it
 *                      to the hardware and will keep the originally requested format in
 *                      the obtained structure.
 *                      If your application can only handle one specific data format, pass a
 *                      zero for allowed_changes and let SDL transparently handle any
 *                      differences.
 *
 * An opened audio device starts out paused, and should be enabled for playing
 * by calling SDL_PauseAudioDevice(devid, 0) when you are ready for your audio
 * callback function to be called.
 */

// [2]
/**
 * This function pauses and unpauses the audio callback processing for a given
 * device. Newly-opened audio devices start in the paused state, so you must
 * call this function with pause_on=0 after opening the specified audio device
 * to start playing sound. This allows you to safely initialize data for your
 * callback function after opening the audio device. Silence will be written to
 * the audio device while paused, and the audio callback is guaranteed to not
 * be called. Pausing one device does not prevent other unpaused devices from
 * running their callbacks.
 *
 * Pausing state does not stack; even if you pause a device several times, a
 * single unpause will start the device playing again, and vice versa. This is
 * different from how SDL_LockAudioDevice() works.
 *
 * If you just need to protect a few variables from race conditions vs your
 * callback, you shouldn't pause the audio device, as it will lead to dropouts
 * in the audio playback. Instead, you should use SDL_LockAudioDevice().
 */

// [3]
/**
 * For decoding, call avcodec_receive_frame(). On success, it will return an
 * AVFrame containing uncompressed audio or video data.
 *
 * Repeat this call until it returns AVERROR(EAGAIN) or an error. The
 * AVERROR(EAGAIN) return value means that new input data is required to return
 * new output. In this case, continue with sending input. For each input
 * frame/packet, the codec will typically return 1 output frame/packet, but
 * it can also be 0 or more than 1.
 */

// [4]
/**
 * This is a hack - the packet memory allocation stuff is broken. The
 * packet is allocated if it was not really allocated.
 *
 * As documented, `av_dup_packet` is broken by design, `av_packet_ref`
 * matches the AVFrame ref-counted API and can be safely used instead.
 */

// [5]
/**
 * avcodec_decode_audio4() is deprecated in FFmpeg 3.1
 *
 * Now that avcodec_decode_audio4 is deprecated and replaced
 * by 2 calls (receive frame and send packet), this could be optimized
 * into separate routines or separate threads.
 *
 * Also now that it always consumes a whole buffer some code
 * in the caller may be able to be optimized.
 *
 * https://github.com/pesintta/vdr-plugin-vaapidevice/issues/32
 */
