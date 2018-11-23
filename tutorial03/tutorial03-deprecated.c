/**
*
*   File:   tutorial03-deprecated.c
*           Keep that all in your head for the moment, because we don't actually
*           have any information yet about the audio streams yet! Let's go back
*           to the place in our code where we found the video stream and find
*           which stream is the audio stream.
*
*           Compiled using
*               gcc -o tutorial03-deprecated tutorial03-deprecated.c -lavutil -lavformat -lavcodec -lswscale -lz -lm  `sdl-config --cflags --libs`
*           on Arch Linux.
*
*           sdl-config just prints out the proper flags for gcc to include the
*           SDL libraries properly. You may need to do something different to
*           get it to compile on your system; please check the SDL documentation
*           for your system. Once it compiles, go ahead and run it.
*
*           Hooray! The video is still going as fast as possible, but the audio
*           is playing in time. Why is this? That's because the audio
*           information has a sample rate — we're pumping out audio information
*           as fast as we can, but the audio simply plays from that stream at
*           its leisure according to the sample rate.We're almost ready to start
*           syncing video and audio ourselves, but first we need to do a little
*           program reorganization. The method of queueing up audio and playing
*           it using a separate thread worked very well: it made the code more
*           managable and more modular. Before we start syncing the video to the
*           audio, we need to make our code easier to deal with. Next time:
*           Spawning Threads!
*
*           Please refer to previous tutorials for uncommented lines of code.
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
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

/**
 * PacketQueue.
 *
 * We are going to be continuously getting packets from the movie file, but at
 * the same time SDL is going to call the callback function! The solution is
 * going to be to create some kind of global structure that we can stuff audio
 * packets in so our audio_callback has something to get audio data from! So
 * what we're going to do is to create a queue of packets. FFmpeg even comes
 * with a structure to help us with this: AVPacketList, which is just a linked
 * list for packets. Here's our queue structure:
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

/**
 * First, we should point out that nb_packets is not the same as size — size
 * refers to a byte size that we get from packet->size. You'll notice that we
 * have a mutex and a condtion variable in there. This is because SDL is
 * running the audio process as a separate thread. If we don't lock the queue
 * properly, we could really mess up our data.
 */
PacketQueue audioq;

/**
 * Global quit flag.
 */
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

    av_dump_format(pFormatCtx, 0, argv[1], 0);

    int i;

    int videoStream = -1;
    int audioStream = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++)
    {
        // find video stream
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0)
        {
            videoStream = i;
        }

        // find audio stream
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStream < 0)
        {
            audioStream = i;
        }
    }

    // exit with error in case video or audio streams are not found

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

    /**
     * From here we can get all the audio related info we want from the
     * AVCodecContext from the stream, just like we did with the video stream:
     */

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

    /**
     * A structure that contains the audio output format. It also contains a
     * callback that is called when the audio device needs more data.
     */
    SDL_AudioSpec wanted_spec;    // 2
    SDL_AudioSpec spec;

    /**
     * Contained within the codec context is all the information we need to set
     * up our audio:
     */

    // set audio settings from codec info           // 3
    wanted_spec.freq = aCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = aCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = aCodecCtx;

    // Finally, we open the audio with SDL_OpenAudio
    ret = SDL_OpenAudio(&wanted_spec, &spec);    // 4
    if (ret < 0)
    {
        printf("SDL_OpenAudio Error: %s\n", SDL_GetError());
        return -1;
    }

    ret = avcodec_open2(aCodecCtx, aCodec, NULL);
    if (ret < 0)
    {
        printf("Could not open audio codec.\n");
        return -1;
    }

    // initi audio queue
    packet_queue_init(&audioq);

    // This function is a legacy means of pausing the audio device. New programs
    // might want to use SDL_PauseAudioDevice() instead.
    SDL_PauseAudio(0);    // 5

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

    // Now we need a place on the screen to put stuff. The basic area for
    // displaying images with SDL is called a surface:
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
        return -1;
    }

    struct SwsContext * sws_ctx = NULL;
    AVPacket * pPacket = av_packet_alloc();
    if (pPacket == NULL)
    {
        printf("Could not alloc packet.\n");
        return -1;
    }

    // Now we create a YUV overlay on that screen so we can input video to it,
    SDL_Overlay * bmp = NULL;
    bmp = SDL_CreateYUVOverlay(
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

    int maxFramesToDecode;
    sscanf(argv[2], "%d", &maxFramesToDecode);

    // used later to handle quit event
    SDL_Event event;

    i = 0;
    while (av_read_frame(pFormatCtx, pPacket) >= 0)
    {
        // video stream packet
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
                    usleep(sleep_time+2);
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
        // audio stream packet
        else if (pPacket->stream_index == audioStream)
        {
            // put the given packet in the audio queue
            packet_queue_put(&audioq, pPacket);
        }
        // everything else
        else
        {
            // just free memory
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
    printf("Usage: ./tutorial03-deprecated <filename> <max-frames-to-decode>\n\n");
    printf("e.g: ./tutorial03-deprecated /home/rambodrahmani/Videos/video.mp4 200\n");
}

/**
 * Initializes the given PacketQueue.
 *
 * @param   q   the PacketQueue to be initialized.
 */
void packet_queue_init(PacketQueue * q)
{
    // Sets the first sizeof(PacketQueue) bytes of the block of memory pointed
    // by q to the specified value "0" (interpreted as an unsigned char).
    memset(
        q,                    // Pointer to the block of memory to fill.
        0,                    // Value to be set. The value is passed as an int,
                              // but the function fills the block of memory
                              // using the unsigned char conversion of this
                              // value.
        sizeof(PacketQueue)   // Number of bytes to be set to the value.
      );

    // Returns the initialized and unlocked mutex or NULL on failure; call
    // SDL_GetError() for more information. Calls to SDL_LockMutex() will not
    // return while the mutex is locked by another thread. See
    // SDL_TryLockMutex() to attempt to lock without blocking.
    q->mutex = SDL_CreateMutex();
    if (!q->mutex)
    {
        // could not create mutex
        printf("SDL_CreateMutex Error: %s\n", SDL_GetError());
        return;
    }

    // Returns a new condition variable or NULL on failure; call SDL_GetError()
    // for more information.
    q->cond = SDL_CreateCond();
    if (!q->cond)
    {
        // could not create condition variable
        printf("SDL_CreateCond Error: %s\n", SDL_GetError());
        return;
    }
}

/**
 * Puts the given AVPacket in the given PacketQueue.
 *
 * @param   q   the queue to be used for the insert.
 * @param   pkt the AVPacket to be inserted in the queue.
 *
 * @return      0 if the AVPacket is correctly inserted in the given PacketQueue.
 */
int packet_queue_put(PacketQueue * q, AVPacket * pkt) // 9
{
    AVPacketList * pkt1;

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

    /**
     * SDL_LockMutex() locks the mutex in the queue so we can add something to
     * it, and then SDL_CondSignal() sends a signal to our get function (if it
     * is waiting) through our condition variable to tell it that there is data
     * and it can proceed, then unlocks the mutex to let it go.
     */

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
 * Gets the first AVPacket from the given PacketQueue.
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
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1); // 7

        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
}

/**
 * This whole process actually starts towards the end of the function, where we
 * call packet_queue_get(). We pick the packet up off the queue, and save its
 * information. Then, once we have a packet to work with, we call
 * avcodec_decode_audio4(), which acts a lot like its sister function,
 * avcodec_decode_video(), except in this case, a packet might have more than
 * one frame, so you may have to call it several times to get all the data out
 * of the packet. Once we have the frame, we simply copy it to our audio buffer,
 * making sure the data_size is smaller than our audio buffer. Also, remember
 * the cast to audio_buf, because SDL gives an 8 bit int buffer, and ffmpeg
 * gives us data in a 16 bit int buffer. You should also notice the difference
 * between len1 and data_size. len1 is how much of the packet we've used, and
 * data_size is the amount of raw data returned. When we've got some data, we
 * immediately return to see if we still need to get more data from the queue,
 * or if we are done. If we still had more of the packet to process, we save it
 * for later. If we finish up a packet, we finally get to free that packet.
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
    static AVPacket pkt = { 0 };
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
        while (audio_pkt_size > 0)
        {
            int got_frame = 0;

            // Decode the audio frame of size pkt->size from pkt->data into
            // frame.
            len1 = avcodec_decode_audio4(aCodecCtx, frame, &got_frame, &pkt); // 8

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
                data_size = av_samples_get_buffer_size(
                    NULL,
                    aCodecCtx->channels,
                    frame->nb_samples,
                    aCodecCtx->sample_fmt,
                    1
                );

                assert(data_size <= buf_size);

                // Copy the values of data_size bytes from the location pointed
                // to by frame->data[0] directly to the memory block pointed to
                // by audio_buf.
                memcpy(audio_buf, frame->data[0], data_size);
            }

            if (data_size <= 0)
            {
                // no data yet, get more frames
                continue;
            }

            // we have the data, return it and come back for more later
            return data_size;
        }

        if (pkt.data)
        {
            av_free_packet(&pkt);
        }

        // global quit flag set, return
        if (quit)
        {
            return -1;
        }

        if (packet_queue_get(&audioq, &pkt, 1) < 0)
        {
            return -1;
        }

        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
    }

    return 0;
}

// [1]
/**
 * This structure is used by SDL_OpenAudioDevice() and SDL_LoadWAV(). While all
 * fields are used by SDL_OpenAudioDevice(), only freq, format, channels, and
 * samples are used by SDL_LoadWAV().freq specifies the number of sample frames
 * sent to the sound device per second. Common values are 11025, 22050, 44100
 * and 48000. Larger values produce cleaner audio, in much the same way that
 * larger resolutions produce cleaner graphics.
 *
 * freq specifies the number of sample frames sent to the sound device per
 * second. Common values are 11025, 22050, 44100 and 48000. Larger values
 * produce cleaner audio, in much the same way that larger resolutions produce
 * cleaner graphics.
 *
 * channels specifies the number of output channels. As of SDL 2.0, supported
 * values are 1 (mono), 2 (stereo), 4 (quad), and 6 (5.1).
 *
 * samples specifies a unit of audio data. When used with SDL_OpenAudioDevice()
 * this refers to the size of the audio buffer in sample frames. A sample frame
 * is a chunk of audio data of the size specified in format multiplied by the
 * number of channels. When the SDL_AudioSpec is used with SDL_LoadWAV()
 * samples is set to 4096. This field's value must be a power of two.
 *
 * The values silence and size are calculated by SDL_OpenAudioDevice().
 *
 * Channel data is interleaved. Stereo samples are stored in left/right
 * ordering. Quad is stored in front-left/front-right/rear-left/rear-right
 * order. 5.1 is stored in front-left/front-right/center/low-freq/rear-left/
 * rear-right ordering ("low-freq" is the ".1" speaker).
 *
 * https://wiki.libsdl.org/SDL_AudioSpec
 */

// [2]
/**
 * A structure that contains the audio output format. It also contains a
 * callback that is called when the audio device needs more data.
 */

// [3]
/**
 * Let's go through these:
 *
 *  freq: The sample rate, as explained earlier.
 *
 *  format: This tells SDL what format we will be giving it. The "S" in
 *  "S16SYS" stands for "signed", the 16 says that each sample is 16 bits
 *  long, and "SYS" means that the endian-order will depend on the system
 *  you are on. This is the format that avcodec_decode_audio2 will give us
 *  the audio in.
 *
 *  channels: Number of audio channels.
 *
 *  silence: This is the value that indicated silence. Since the audio is
 *  signed, 0 is of course the usual value.
 *
 *  samples: This is the size of the audio buffer that we would like SDL to
 *  give us when it asks for more audio. A good value here is between 512
 *  and 8192; ffplay uses 1024.
 *
 *  callback: Here's where we pass the actual callback function. We'll talk
 *  more about the callback function later.
 *
 *  userdata: SDL will give our callback a void pointer to any user data
 *  that we want our callback function to have. We want to let it know
 *  about our codec context; you'll see why.
 */

// [4]
/**
 * This function is a legacy means of opening the audio device. New programs
 * might want to use SDL_OpenAudioDevice() instead.
 *
 * This function opens the audio device with the desired parameters, and
 * returns 0 if successful, placing the actual hardware parameters in the
 * structure pointed to by spec.
 *
 * If par is NULL, the audio data passed to the callback function will be
 * guaranteed to be in the requested format, and will be automatically converted
 * to the actual hardware audio format if necessary. If par is NULL, wanted_spec
 * will have fields modified.
 *
 * This function returns a negative error code on failure to open the audio
 * device or failure to set up the audio thread; call SDL_GetError() for more
 * information.
 */

// [5]
/**
 * SDL_PauseAudio(1);   // audio callback is stopped when this returns.
 * SDL_Delay(5000);     // audio device plays silence for 5 seconds
 * SDL_PauseAudio(0);   // audio callback starts running again.
 */

// [6]
/**
 * Notice how SDL_CondWait() makes the function block (i.e. pause
 * until we get data) if we tell it to.
 * As you can see, we've wrapped the function in a forever loop so
 * we will be sure to get some data if we want to block. We avoid
 * looping forever by making use of SDL's SDL_CondWait() function.
 * Basically, all CondWait does is wait for a signal from
 * SDL_CondSignal() (or SDL_CondBroadcast()) and then continue.
 * However, it looks as though we've trapped it within our mutex —
 * if we hold the lock, our put function can't put anything in the
 * queue! However, what SDL_CondWait() also does for us is to
 * unlock the mutex we give it and then attempt to lock it again
 * once we get the signal.
 */

// [7]
/*
 * The underlying type of the objects pointed to by both the source and
 * destination pointers are irrelevant for this function; The result is a
 * binary copy of the data.
 *
 * The function does not check for any terminating null character in source -
 * it always copies exactly num bytes.
 *
 * To avoid overflows, the size of the arrays pointed to by both the
 * destination and source parameters, shall be at least num bytes, and should
 * not overlap (for overlapping memory blocks, memmove is a safer approach).
 */

// [8]
/**
 * Some decoders may support multiple frames in a single AVPacket. Such
 * decoders would then just decode the first frame and the return value would
 * be less than the packet size. In this case, avcodec_decode_audio4 has to be
 * called again with an AVPacket containing the remaining data in order to
 * decode the second frame, etc... Even if no frames are returned, the packet
 * needs to be fed to the decoder with remaining data until it is completely
 * consumed or an error occurs.
 *
 * Some decoders (those marked with CODEC_CAP_DELAY) have a delay between input
 * and output. This means that for some packets they will not immediately
 * produce decoded output and need to be flushed at the end of decoding to get
 * all the decoded data. Flushing is done by calling this function with packets
 * with avpkt->data set to NULL and avpkt->size set to 0 until it stops
 * returning samples. It is safe to flush even those decoders that are not
 * marked with CODEC_CAP_DELAY, then no samples will be returned.
 */

// [9]
/**
 * Queues are a type of container adaptor, specifically designed to operate in
 * a FIFO context (first-in first-out), where elements are inserted into one
 * end of the container and extracted from the other.
 *
 * Queues are implemented as containers adaptors, which are classes that use an
 * encapsulated object of a specific container class as its underlying
 * container, providing a specific set of member functions to access its
 * elements. Elements are pushed into the "back" of the specific container and
 * popped from its "front".
 */
