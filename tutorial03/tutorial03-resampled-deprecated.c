/**
*
*   File:   tutorial03-resampled-deprecated.c
*           This tutorial adds resampling to the tutorial03-deprecated.c in order
*           to be able to obtain a clear output sound.
*
*           Compiled using
*               gcc -o tutorial03-resampled-deprecated tutorial03-resampled-deprecated.c -lavutil -lavformat -lavcodec -lswscale -lswresample -lz -lm  `sdl-config --cflags --libs`
*           on Arch Linux.
*
*           Please refer to previous tutorials for uncommented lines of code.
*           This implementation uses deprecated APIs. Please refer to
*           tutorial03-resampled.c for an implementation using new APIs.
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
#include <libswresample/swresample.h>     // [0]
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

/**
 * PacketQueue.
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

PacketQueue audioq;

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

static int audio_resampling(AVCodecContext * audio_decode_ctx,
                            AVFrame * audio_decode_frame,
                            enum AVSampleFormat out_sample_fmt,
                            int out_channels,
                            int out_sample_rate,
                            uint8_t *out_buf);

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

    int videoStream = -1;
    int audioStream = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++)
    {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0)
        {
            videoStream = i;
        }

        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStream < 0)
        {
            audioStream = i;
        }
    }

    if (videoStream == -1)
    {
        printf("Could not find video stream.\n");
        return -1;
    }

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
    SDL_AudioSpec wanted_spec;
    SDL_AudioSpec spec;

    // set audio settings from codec info
    wanted_spec.freq = aCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = aCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = aCodecCtx;

    // Finally, we open the audio with SDL_OpenAudio
    // This function is a legacy means of opening the audio device. New
    // programs might want to use SDL_OpenAudioDevice() instead.
    // Please refer to tutorial03-resampled.c
    if (SDL_OpenAudio(&wanted_spec, &spec) < 0)
    {
        printf("SDL_OpenAudio: %s\n", SDL_GetError());
        return -1;
    }

    ret = avcodec_open2(aCodecCtx, aCodec, NULL);
    if (ret < 0)
    {
        printf("Could not open audio codec.\n");
        return -1;
    }

    // init audio PacketQueue
    packet_queue_init(&audioq);

    // start playing audio on the first audio device
    SDL_PauseAudio(0);

    AVCodec * pCodec = NULL;
    pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
    if (pCodec == NULL)
    {
        printf("Unsupported codec!\n");
        return -1;
    }

    AVCodecContext * pCodecCtx = NULL;
    pCodecCtx = avcodec_alloc_context3(pCodec);
    ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
    if (ret != 0)
    {
        printf("Could not copy video codec context.\n");
        return -1;
    }

    ret = avcodec_open2(pCodecCtx, pCodec, NULL);
    if (ret < 0)
    {
        printf("Could not open video codec.\n");
        return -1;
    }

    AVFrame * pFrame = NULL;
    pFrame = av_frame_alloc();
    if (pFrame == NULL)
    {
        printf("Could not allocate video frame.\n");
        return -1;
    }

    // Now we need a place on the screen to put stuff.
    SDL_Surface * screen;
    #ifndef __DARWIN__
            screen = SDL_SetVideoMode(pCodecCtx->width/2, pCodecCtx->height/2, 0, 0);
    #else
            screen = SDL_SetVideoMode(pCodecCtx->width/2, pCodecCtx->height/2, 24, 0);
    #endif
    if (!screen)
    {
        // could not set video mode
        printf("SDL: could not set video mode - exiting.\n");

        // exit with Error
        return -1;
    }

    AVPacket * pPacket = av_packet_alloc();
    if (pPacket == NULL)
    {
        printf("Could not alloc packet,\n");
        return -1;
    }

    // and set up our SWSContext to convert the image data to YUV420:
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

    // Now we create a YUV overlay on that screen so we can input video to it,
    SDL_Overlay * bmp = NULL;
    bmp = SDL_CreateYUVOverlay(
            pCodecCtx->width,
            pCodecCtx->height,
            SDL_YV12_OVERLAY,
            screen
    );

    int maxFramesToDecode;
    sscanf(argv[2], "%d", &maxFramesToDecode);

    // used later to handle quit event
    SDL_Event event;

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

                SDL_LockYUVOverlay(bmp);

                AVPicture pict;

                pict.data[0] = bmp->pixels[0];
                pict.data[1] = bmp->pixels[2];
                pict.data[2] = bmp->pixels[1];

                pict.linesize[0] = bmp->pitches[0];
                pict.linesize[1] = bmp->pitches[2];
                pict.linesize[2] = bmp->pitches[1];

                sws_scale(
                    sws_ctx,
                    (uint8_t const * const *)pFrame->data,
                    pFrame->linesize,
                    0,
                    pCodecCtx->height,
                    pict.data,
                    pict.linesize
                );

                SDL_UnlockYUVOverlay(bmp);

                if (++i <= maxFramesToDecode)
                {
                    // dumb audio video sync
                    /*double fps = av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate);
                    double sleep_time = 1.0/(double)fps;
                    usleep(sleep_time+4);
                    SDL_Delay((1000 * sleep_time) - 10);*/

                    SDL_Rect rect;
                    rect.x = 0;
                    rect.y = 0;
                    rect.w = pCodecCtx->width/2;
                    rect.h = pCodecCtx->height/2;

                    // Blit the overlay to the display surface specified when the
                    // overlay was created.
                    SDL_DisplayYUVOverlay(bmp, &rect);

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
        else if (pPacket->stream_index == audioStream)
        {
            packet_queue_put(&audioq, pPacket);
        }
        else
        {
            av_packet_unref(pPacket);
        }

        // handle quit event
        SDL_PollEvent(&event);
        switch(event.type)
        {
            case SDL_QUIT:
            {
                SDL_Quit();
                quit = 1;
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
    av_packet_unref(pPacket);

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
    printf("Usage: ./tutorial03-resampled-deprecated <filename> <max-frames-to-decode>\n\n");
    printf("e.g: ./tutorial03-resampled-deprecated /home/rambodrahmani/Videos/video.mp4 200\n");
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
        printf("SDL_CreateMutex Error: %s\n", SDL_GetError());
        return;
    }

    // Returns a new condition variable or NULL on failure
    q->cond = SDL_CreateCond();
    if (!q->cond)
    {
        // could not create condition variable
        printf("SDL_CreateCond Error: %s\n", SDL_GetError());
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
int packet_queue_put(PacketQueue * q, AVPacket * pkt) // 9
{
    AVPacketList * pkt1;

    // This is a hack - the packet memory allocation stuff is broken. The
    // packet is allocated if it was not really allocated.
    if (av_dup_packet(pkt) < 0)
    {
        return -1;
    }

    pkt1 = av_malloc(sizeof(AVPacketList));

    if (!pkt1)
    {
        return -1;
    }

    pkt1->pkt = * pkt;
    pkt1->next = NULL;

    // lock mutex
    SDL_LockMutex(q->mutex);

    // check the queue is empty
    if (!q->last_pkt)
    {
        // if it is, insert as first
        q->first_pkt = pkt1;
    }
    else
    {
        // if not, insert as last
        q->last_pkt->next = pkt1;
    }

    // point the last AVPacketList in the queue to the newly inserted
    // AVPacketList.
    q->last_pkt = pkt1;

    // increase by 1 the number of AVPackets in the queue.
    q->nb_packets++;

    // increase queue size by adding the size of the newly inserted AVPacket.
    q->size += pkt1->pkt.size;

    // notify packet_queue_get which is waiting that a new packet is available.
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
static int packet_queue_get(PacketQueue * q, AVPacket * pkt, int block) // 9
{
    AVPacketList * pkt1;

    int ret;

    SDL_LockMutex(q->mutex);

    for (;;)
    {
        // check quit flag
        if (quit)
        {
            ret = -1;
            break;
        }

        // point the first packet in the queue
        pkt1 = q->first_pkt;

        // if the first packet is not NULL, the queue is not empty
        if (pkt1)
        {
            // place the second packet in the queue at first position
            q->first_pkt = pkt1->next;

            // check if queue is empty after removal
            if (!q->first_pkt)
            {
                // first_pkt = last_pkt = NULL = empty queue
                q->last_pkt = NULL;
            }

            // decrease the number of packets in the queue
            q->nb_packets--;

            // decrease the size of the packets in the queue
            q->size -= pkt1->pkt.size;

            // point pkt to the extracted packet, this will return to the calling
            // function
            *pkt = pkt1->pkt;

            // free memory
            av_free(pkt1);

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
            SDL_CondWait(q->cond, q->mutex);    // 6
        }
    }

    SDL_UnlockMutex(q->mutex);

    return ret;
}

/**
 * This is basically a simple loop that will pull in data from another function
 * we will write, audio_decode_frame(), store the result in an intermediary
 * buffer, attempt to write len bytes to stream, and get more data if we don't
 * have enough yet, or save it for later if we have some left over.
 *
 * @param   userdata    is the pointer we gave to SDL.
 * @param   stream      is the buffer we will be writing audio data to.
 * @param   len         is the size of that buffer.
 */
void audio_callback(void * userdata, Uint8 * stream, int len)
{
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
 * we have the frame, we simply copy it to our audio buffer, making sure the
 * data_size is smaller than our audio buffer. This is a new version of the
 * previous audio_decode_frame method (tutorial03-deprecated.c) with audio resampling
 * applied.
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
    AVPacket * pkt = av_packet_alloc();
    static uint8_t * audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    static AVFrame * frame = NULL;

    if (!frame)
    {
        // allocate a new frame, ued to decode audio packets
        frame = av_frame_alloc();
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

            // Decode the audio frame of size pkt->size from pkt->data into
            // frame.
            len1 = avcodec_decode_audio4(aCodecCtx, frame, &got_frame, pkt); // 8

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
                                frame,
                                AV_SAMPLE_FMT_S16,
                                aCodecCtx->channels,
                                aCodecCtx->sample_rate,
                                audio_buf
                            );

                //data_size = av_samples_get_buffer_size(
                //    NULL,
                //    aCodecCtx->channels,
                //    frame->nb_samples,
                //    aCodecCtx->sample_fmt,
                //    1
                //);

                assert(data_size <= buf_size);

                // the memory management is done in the audio_resampling().
                // memcpy(audio_buf, frame->data[0], data_size);
            }

            if (data_size <= 0)
            {
                // no data yet, get more frames
                continue;
            }

            // we have the data, return it and come back for more later
            return data_size;
        }

        if (pkt->data)
        {
            av_free_packet(pkt);
        }

        if (packet_queue_get(&audioq, pkt, 1) < 0)
        {
            return -1;
        }

        audio_pkt_data = pkt->data;
        audio_pkt_size = pkt->size;
    }

    return 0;
}

/**
 * Resample the audio data retrieved using FFmpeg before playing it.
 *
 * @param   audio_decode_ctx    the audio codec context retrieved from the original AVFormatContext.
 * @param   audio_decode_frame  the decoded audio frame.
 * @param   out_sample_fmt      audio output sample format (e.g. AV_SAMPLE_FMT_S16).
 * @param   out_channels        audio output channels, retrieved from the original audio codec context.
 * @param   out_sample_rate     audio output sample rate, retrieved from the original audio codec context.
 * @param   out_buf             audio output buffer.
 *
 * @return                      the size of the resampled audio data.
 */
static int audio_resampling(                                  // 1
                        AVCodecContext * audio_decode_ctx,
                        AVFrame * audio_decode_frame,
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

    in_nb_samples = audio_decode_frame->nb_samples;
    if (in_nb_samples <= 0)
    {
        printf("in_nb_samples error.\n");
        return -1;
    }

    // These functions call set for the given obj the field with the given name
    // to the specified value.
    av_opt_set_int(   // 3
        swr_ctx,
        "in_channel_layout",
        in_channel_layout,
        0
    );

    av_opt_set_int(
        swr_ctx,
        "in_sample_rate",
        audio_decode_ctx->sample_rate,
        0
    );

    av_opt_set_sample_fmt(
        swr_ctx,
        "in_sample_fmt",
        audio_decode_ctx->sample_fmt,
        0
    );

    av_opt_set_int(
        swr_ctx,
        "out_channel_layout",
        out_channel_layout,
        0
    );

    av_opt_set_int(
        swr_ctx,
        "out_sample_rate",
        out_sample_rate,
        0
    );

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
        printf("Failed to initialize the resampling context\n");
        return -1;
    }

    /*
     * Compute the number of converted samples: buffering is avoided
     * ensuring that the output buffer will contain at least all the
     * converted input samples.
     */
    max_out_nb_samples = out_nb_samples = av_rescale_rnd(
                                                      in_nb_samples,
                                                      out_sample_rate,
                                                      audio_decode_ctx->sample_rate,
                                                      AV_ROUND_UP
                                                  );

    // check rounding was successful
    if (max_out_nb_samples <= 0)
    {
        printf("av_rescale_rnd error.\n");
        return -1;
    }

    // get number of output audio channels
    out_nb_channels = av_get_channel_layout_nb_channels(out_channel_layout);

    // Allocate a data pointers array and samples buffer for nb_samples samples
    ret = av_samples_alloc_array_and_samples( // [7]
              &resampled_data,
              &out_linesize,
              out_nb_channels,
              out_nb_samples,
              out_sample_fmt,
              0
          );

    if (ret < 0)
    {
        printf("av_samples_alloc_array_and_samples error: Could not allocate destination samples.\n");
        return -1;
    }

    out_nb_samples = av_rescale_rnd(
                        swr_get_delay(swr_ctx, audio_decode_ctx->sample_rate) + in_nb_samples,  // [4]
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
        ret = av_samples_alloc(   // [6]
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
        // The conversion itself is done by repeatedly calling swr_convert().
        // Note that the samples may get buffered in swr if you provide
        // insufficient output space or if sample rate conversion is done,
        // which requires "future" samples. Samples that do not require future
        // input can be retrieved at any time by using swr_convert() (in_count
        // can be set to 0). At the end of conversion the resampling buffer can
        // be flushed by calling swr_convert() with NULL in and 0 in_count.
        ret = swr_convert(
                  swr_ctx,
                  resampled_data,
                  out_nb_samples,
                  (const uint8_t **) audio_decode_frame->data,
                  audio_decode_frame->nb_samples
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
            printf("av_samples_get_buffer_size error\n");
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
     * Clean up.
     */
    if (resampled_data)
    {
        // free memory block and set pointer to NULL
        av_freep(&resampled_data[0]);   // [5]
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

// [0]
/*
 * Libswresample (lswr) is a library that handles audio resampling, sample
 * format conversion and mixing.
 *
 * Interaction with lswr is done through SwrContext, which is allocated with
 * swr_alloc() or swr_alloc_set_opts(). It is opaque, so all parameters must
 * be set with the AVOptions API.
 *
 * The first thing you will need to do in order to use lswr is to allocate
 * SwrContext. This can be done with swr_alloc() or swr_alloc_set_opts(). If
 * you are using the former, you must set options through the AVOptions API.
 * The latter function provides the same feature, but it allows you to set
 * some common options in the same statement.
 */

// [1]
/**
 * Sample-rate conversion is the process of changing the sampling rate of a
 * discrete signal to obtain a new discrete representation of the underlying
 * continuous signal. Application areas include image scaling and audio/visual
 * systems, where different sampling rates may be used for engineering,
 * economic, or historical reasons. For example, Compact Disc Digital Audio and
 * Digital Audio Tape systems use different sampling rates, and American
 * television, European television, and movies all use different frame rates.
 * Sample-rate conversion prevents changes in speed and pitch that would
 * otherwise occur when transferring recorded material between such systems.
 *
 * Within specific domains or for specific conversions, the following
 * alternative terms for sample-rate conversion are also used:
 * sampling-frequency conversion, resampling, upsampling, downsampling,
 * interpolation, decimation, upscaling, downscaling. The term multi-rate
 * digital signal processing is sometimes used to refer to systems that
 * incorporate sample-rate conversion.
 */

// [2]
/*
 * Return the number of channels in the channel layout.
 */

// [3]
/**
 * Those functions set the field of obj with the given name to value.
 * Parameters:
 *    [in] 	obj       A struct whose first element is a pointer to an AVClass.
 *    [in] 	name      the name of the field to set
 *    [in] 	val       The value to set. In case of av_opt_set() if the field is
 *                    not of a string type, then the given string is parsed. SI
 *                    postfixes and some named scalars are supported. If the
 *                    field is of a numeric type, it has to be a numeric or
 *                    named scalar. Behavior with more than one scalar and +-
 *                    infix operators is undefined. If the field is of a flags
 *                    type, it has to be a sequence of numeric scalars or named
 *                    flags separated by '+' or '-'. Prefixing a flag with '+'
 *                    causes it to be set without affecting the other flags;
 *                    similarly, '-' unsets a flag.
 *    search_flags    flags passed to av_opt_find2. I.e. if
 *                    AV_OPT_SEARCH_CHILDREN is passed here, then the option
 *                    may be set on a child of obj.
 */

// [4]
/**
 * Gets the delay the next input sample will experience relative to the next
 * output sample. Swresample can buffer data if more input has been provided
 * than available output space, also converting between sample rates needs a
 * delay. This function returns the sum of all such delays. The exact delay is
 * not necessarily an integer value in either input or output sample rate.
 * Especially when downsampling by a large value, the output sample rate may
 * be a poor choice to represent the delay, similarly for upsampling and the
 * input sample rate.
 */

// [5]
/**
 * Free a memory block which has been allocated with av_malloc(z)() or
 * av_realloc() and set the pointer pointing to it to NULL.
 */

// [6]
/**
 * Allocate a samples buffer for nb_samples samples, and fill data pointers and
 * linesize accordingly.
 * The allocated samples buffer can be freed by using av_freep(&audio_data[0])
 * Allocated data will be initialized to silence.
 *
 * Returns >=0 on success or a negative error code on failure
 */

// [7]
/**
 * Allocate a data pointers array, samples buffer for nb_samples samples, and
 * fill data pointers and linesize accordingly.
 * This is the same as av_samples_alloc(), but also allocates the data pointers
 * array.
 */
