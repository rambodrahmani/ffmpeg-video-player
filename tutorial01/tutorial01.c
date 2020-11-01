/**
 *
 *   File:   tutorial01.c
 *           First, let's see how we open a file in the first place.
 *           This implementation uses the new FFmpeg API.
 *
 *           Compiled using
 *               $ gcc -o tutorial01 tutorial01.c -lavutil -lavformat -lavcodec -lswscale -lz -lm
 *           on Arch Linux.
 *           You can also compile all the source files in this repo using the
 *           provided CMake files using
 *           	$ cmake CMakeLists.txt -Bcmake-build-debug
 *           	$ cd cmake-build-debug/
 *           	$ make
 *
 *   Author: Rambod Rahmani <rambodrahmani@autistici.org>
 *           Created on 8/6/18.
 *
 **/

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

void printHelpMenu();
void saveFrame(AVFrame * avFrame, int width, int height, int frameIndex);

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
    // with ffmpeg, you have to first initialize the library.
    // 'av_register_all' is deprecated just omit this function call in ffmpeg
    // 4.0 and later.
    // av_register_all();  // [0]

    // we get our filename from the first argument, check if the file name is
    // provided, show help menu if not
    if ( !(argc > 2) )
    {
        // wrong arguments, print help menu
        printHelpMenu();

        // exit with error
        return -1;
    }

    // declare the AVFormatContext
    AVFormatContext * pFormatCtx = NULL; // [1]

    // now we can actually open the file:
    // the minimum information required to open a file is its URL, which is
    // passed to avformat_open_input(), as in the following code:
    int ret = avformat_open_input(&pFormatCtx, argv[1], NULL, NULL);    // [2]
    if (ret < 0)
    {
        // couldn't open file
        printf("Could not open file %s\n", argv[1]);

        // exit with error
        return -1;
    }

    // The call to avformat_open_input(), only looks at the header, so next we
    // need to check out the stream information in the file.:
    // Retrieve stream information
    ret = avformat_find_stream_info(pFormatCtx, NULL);  //[3]
    if (ret < 0)
    {
        // couldn't find stream information
        printf("Could not find stream information %s\n", argv[1]);

        // exit with error
        return -1;
    }

    // We introduce a handy debugging function to show us what's inside dumping
    // information about file onto standard error
    av_dump_format(pFormatCtx, 0, argv[1], 0);  // [4]

    // Now pFormatCtx->streams is just an array of pointers, of size
    // pFormatCtx->nb_streams, so let's walk through it until we find a video
    // stream.
    int i;

    // The stream's information about the codec is in what we call the
    // "codec context." This contains all the information about the codec that
    // the stream is using
    AVCodecContext * pCodecCtxOrig = NULL;
    AVCodecContext * pCodecCtx = NULL;

    // Find the first video stream
    int videoStream = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++)
    {
        // check the General type of the encoded data to match
	// AVMEDIA_TYPE_VIDEO
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) // [5]
        {
            videoStream = i;
            break;
        }
    }

    if (videoStream == -1)
    {
        // didn't find a video stream
        return -1;
    }

    /**
     * New API.
     * This implementation uses the new API.
     * Please refer to tutorial01-deprecated.c for an implementation using the
     * deprecated FFmpeg API.
     */

    // Get a pointer to the codec context for the video stream.
    // AVStream::codec deprecated
    // https://ffmpeg.org/pipermail/libav-user/2016-October/009801.html
    // pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;

    // But we still have to find the actual codec and open it:
    AVCodec * pCodec = NULL;

    // Find the decoder for the video stream
    pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id); // [6]
    if (pCodec == NULL)
    {
        // codec not found
        printf("Unsupported codec!\n");

        // exit with error
        return -1;
    }

    pCodecCtxOrig = avcodec_alloc_context3(pCodec); // [7]
    ret = avcodec_parameters_to_context(pCodecCtxOrig, pFormatCtx->streams[videoStream]->codecpar);

    /**
     * Note that we must not use the AVCodecContext from the video stream
     * directly! So we have to use avcodec_copy_context() to copy the
     * context to a new location (after allocating memory for it, of
     * course).
     */

    // Copy context
    // avcodec_copy_context deprecation
    // http://ffmpeg.org/pipermail/libav-user/2017-September/010615.html
    //ret = avcodec_copy_context(pCodecCtx, pCodecCtxOrig);
    pCodecCtx = avcodec_alloc_context3(pCodec); // [7]
    ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
    if (ret != 0)
    {
        // error copying codec context
        printf("Could not copy codec context.\n");

        // exit with error
        return -1;
    }

    // Open codec
    ret = avcodec_open2(pCodecCtx, pCodec, NULL);   // [8]
    if (ret < 0)
    {
        // Could not open codec
        printf("Could not open codec.\n");

        // exit with error
        return -1;
    }

    // Now we need a place to actually store the frame:
    AVFrame * pFrame = NULL;

    // Allocate video frame
    pFrame = av_frame_alloc();  // [9]
    if (pFrame == NULL)
    {
        // Could not allocate frame
        printf("Could not allocate frame.\n");

        // exit with error
        return -1;
    }

    /**
     * Since we're planning to output PPM files, which are stored in 24-bit
     * RGB, we're going to have to convert our frame from its native format
     * to RGB. ffmpeg will do these conversions for us. For most projects
     * (including ours) we're going to want to convert our initial frame to
     * a specific format. Let's allocate a frame for the converted frame
     * now.
     */

    // Allocate an AVFrame structure
    AVFrame * pFrameRGB = NULL;
    pFrameRGB = av_frame_alloc();
    if (pFrameRGB == NULL)
    {
        // Could not allocate frame
        printf("Could not allocate frame.\n");

        // exit with error
        return -1;
    }

    // Even though we've allocated the frame, we still need a place to put
    // the raw data when we convert it. We use avpicture_get_size to get
    // the size we need, and allocate the space manually:
    uint8_t * buffer = NULL;
    int numBytes;

    // Determine required buffer size and allocate buffer
    // numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
    // https://ffmpeg.org/pipermail/ffmpeg-devel/2016-January/187299.html
    // what is 'linesize alignment' meaning?:
    // https://stackoverflow.com/questions/35678041/what-is-linesize-alignment-meaning
    numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 32); // [10]
    buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));    // [11]

    /**
    * Now we use avpicture_fill() to associate the frame with our newly
    * allocated buffer. About the AVPicture cast: the AVPicture struct is
    * a subset of the AVFrame struct - the beginning of the AVFrame struct
    * is identical to the AVPicture struct.
    */
    // Assign appropriate parts of buffer to image planes in pFrameRGB
    // Note that pFrameRGB is an AVFrame, but AVFrame is a superset
    // of AVPicture
    // Picture data structure - Deprecated: use AVFrame or imgutils functions
    // instead
    // https://www.ffmpeg.org/doxygen/3.0/structAVPicture.html#a40dfe654d0f619d05681aed6f99af21b
    // avpicture_fill( // [12]
    //     (AVPicture *)pFrameRGB,
    //     buffer,
    //     AV_PIX_FMT_RGB24,
    //     pCodecCtx->width,
    //     pCodecCtx->height
    // );
    av_image_fill_arrays( // [12]
        pFrameRGB->data,
        pFrameRGB->linesize,
        buffer,
        AV_PIX_FMT_RGB24,
        pCodecCtx->width,
        pCodecCtx->height,
        32
    );

    // Finally! Now we're ready to read from the stream!

    /**
     * What we're going to do is read through the entire video stream by
     * reading in the packet, decoding it into our frame, and once our
     * frame is complete, we will convert and save it.
     */

    struct SwsContext * sws_ctx = NULL;

    AVPacket * pPacket = av_packet_alloc();
    if (pPacket == NULL)
    {
        // couldn't allocate packet
        printf("Could not alloc packet,\n");

        // exit with error
        return -1;
    }

    // initialize SWS context for software scaling
    sws_ctx = sws_getContext(   // [13]
        pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        pCodecCtx->width,
        pCodecCtx->height,
        AV_PIX_FMT_RGB24,   // sws_scale destination color scheme
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );

    // The numer in the argv[2] array is in a string representation. We
    // need to convert it to an integer.
    int maxFramesToDecode;
    sscanf (argv[2], "%d", &maxFramesToDecode);

    /**
     * The process, again, is simple: av_read_frame() reads in a packet and
     * stores it in the AVPacket struct. Note that we've only allocated the
     * packet structure - ffmpeg allocates the internal data for us, which
     * is pointed to by packet.data. This is freed by the av_free_packet()
     * later. avcodec_decode_video() converts the packet to a frame for us.
     * However, we might not have all the information we need for a frame
     * after decoding a packet, so avcodec_decode_video() sets
     * frameFinished for us when we have decoded enough packets the next
     * frame.
     * Finally, we use sws_scale() to convert from the native format
     * (pCodecCtx->pix_fmt) to RGB. Remember that you can cast an AVFrame
     * pointer to an AVPicture pointer. Finally, we pass the frame and
     * height and width information to our SaveFrame function.
     */

    i = 0;
    while (av_read_frame(pFormatCtx, pPacket) >= 0)  // [14]
    {
        // Is this a packet from the video stream?
        if (pPacket->stream_index == videoStream)
        {
            // Decode video frame
            // avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &pPacket);
            // Deprecated: Use avcodec_send_packet() and avcodec_receive_frame().
            ret = avcodec_send_packet(pCodecCtx, pPacket);    // [15]
            if (ret < 0)
            {
                // could not send packet for decoding
                printf("Error sending packet for decoding.\n");

                // exit with eror
                return -1;
            }

            while (ret >= 0)
            {
                ret = avcodec_receive_frame(pCodecCtx, pFrame);   // [15]

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    // EOF exit loop
                    break;
                }
                else if (ret < 0)
                {
                    // could not decode packet
                    printf("Error while decoding.\n");

                    // exit with error
                    return -1;
                }

                // Convert the image from its native format to RGB
                sws_scale(  // [16]
                    sws_ctx,
                    (uint8_t const * const *)pFrame->data,
                    pFrame->linesize,
                    0,
                    pCodecCtx->height,
                    pFrameRGB->data,
                    pFrameRGB->linesize
                );

                // Save the frame to disk
                if (++i <= maxFramesToDecode)
                {
                    // save the read AVFrame into ppm file
                    saveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, i);

                    // print log information
                    printf(
                        "Frame %c (%d) pts %d dts %d key_frame %d "
			"[coded_picture_number %d, display_picture_number %d,"
			" %dx%d]\n",
                        av_get_picture_type_char(pFrame->pict_type),
                        pCodecCtx->frame_number,
                        pFrameRGB->pts,
                        pFrameRGB->pkt_dts,
                        pFrameRGB->key_frame,
                        pFrameRGB->coded_picture_number,
                        pFrameRGB->display_picture_number,
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
                // exit loop and terminate
                break;
            }
        }

        // Free the packet that was allocated by av_read_frame
        // [FFmpeg-cvslog] avpacket: Replace av_free_packet with
        // av_packet_unref
        // https://lists.ffmpeg.org/pipermail/ffmpeg-cvslog/2015-October/094920.html
        av_packet_unref(pPacket);
    }

    /**
     * Cleanup.
     */

    // Free the RGB image
    av_free(buffer);
    av_frame_free(&pFrameRGB);
    av_free(pFrameRGB);

    // Free the YUV frame
    av_frame_free(&pFrame);
    av_free(pFrame);

    // Close the codecs
    avcodec_close(pCodecCtx);
    avcodec_close(pCodecCtxOrig);

    // Close the video file
    avformat_close_input(&pFormatCtx);

    return 0;
}

/**
 * Print help menu containing usage information.
 */
void printHelpMenu()
{
    printf("Invalid arguments.\n\n");
    printf("Usage: ./tutorial01 <filename> <max-frames-to-decode>\n\n");
    printf("e.g: ./tutorial01 /home/rambodrahmani/Videos/Labrinth-Jealous.mp4 200\n");
}

/**
 * Write the given AVFrame into a .ppm file.
 *
 * @param   avFrame     the AVFrame to be saved.
 * @param   width       the given frame width as obtained by the AVCodecContext.
 * @param   height      the given frame height as obtained by the AVCodecContext.
 * @param   frameIndex  the given frame index.
 */
void saveFrame(AVFrame *avFrame, int width, int height, int frameIndex)
{
    FILE * pFile;
    char szFilename[32];
    int  y;

    /**
     * We do a bit of standard file opening, etc., and then write the RGB data.
     * We write the file one line at a time. A PPM file is simply a file that
     * has RGB information laid out in a long string. If you know HTML colors,
     * it would be like laying out the color of each pixel end to end like
     * #ff0000#ff0000.... would be a red screen. (It's stored in binary and
     * without the separator, but you get the idea.) The header indicated how
     * wide and tall the image is, and the max size of the RGB values.
     */

    // Open file
    sprintf(szFilename, "frame%d.ppm", frameIndex);
    pFile = fopen(szFilename, "wb");
    if (pFile == NULL)
    {
        return;
    }

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write pixel data
    for (y = 0; y < height; y++)
    {
        fwrite(avFrame->data[0] + y * avFrame->linesize[0], 1, width * 3, pFile);
    }

    // Close file
    fclose(pFile);
}

// [0]
/*
* With ffmpeg, you have to first initialize the library.
* Initialize libavformat and register all the muxers, demuxers and protocols.
*
* This registers all available file formats and codecs with the
* library so they will be used automatically when a file with the
* corresponding format/codec is opened. Note that you only need to call
* av_register_all() once, so we do it here in main(). If you like, it's
* possible to register only certain individual file formats and codecs,
* but there's usually no reason why you would have to do that.
*
* av_register_all() has been deprecated in ffmpeg 4.0, it is no longer
* necessary to call av_register_all().
*/

// [1]
/**
* Format I/O context.
*
* Libavformat (lavf) is a library for dealing with various media container
* formats. Its main two purposes are demuxing - i.e. splitting a media file
* into component streams, and the reverse process of muxing - writing supplied
* data in a specified container format. It also has an lavf_io
* "I/O module" which supports a number of protocols for accessing the data (e.g.
* file, tcp, http and others). Before using lavf, you need to call
* av_register_all() to register all compiled muxers, demuxers and protocols.
* Unless you are absolutely sure you won't use libavformat's network
* capabilities, you should also call avformat_network_init().
*
* Main lavf structure used for both muxing and demuxing is AVFormatContext,
* which exports all information about the file being read or written. As with
* most Libav structures, its size is not part of public ABI, so it cannot be
* allocated on stack or directly with av_malloc(). To create an
* AVFormatContext, use avformat_alloc_context() (some functions, like
* avformat_open_input() might do that for you).
*
* Most importantly an AVFormatContext contains:
* the AVFormatContext.iformat "input" or AVFormatContext.oformat
* "output" format. It is either autodetected or set by user for input;
* always set by user for output.
* an AVFormatContext.streams "array" of AVStreams, which describe all
* elementary streams stored in the file. AVStreams are typically referred to
* using their index in this array.
* an AVFormatContext.pb "I/O context". It is either opened by lavf or
* set by user for input, always set by user for output (unless you are dealing
* with an AVFMT_NOFILE format).
*/

// [2]
/**
 * We get our filename from the first argument. This function reads the file
 * header and stores information about the file format in the AVFormatContext
 * structure we have given it. The last three arguments are used to specify
 * the file format, buffer size, and format options, but by setting this to
 * NULL or 0, libavformat will auto-detect these.
 *
 * Demuxers read a media file and split it into chunks of data (packets). A
 * packet contains one or more encoded frames which belongs to a single
 * elementary stream. In the lavf API this process is represented by the
 * avformat_open_input() function for opening a file, av_read_frame() for
 * reading a single packet and finally avformat_close_input(), which does the
 * cleanup.
 *
 * The above code attempts to allocate an AVFormatContext, open the specified
 * file (autodetecting the format) and read the header, exporting the
 * information stored there into the given AVFormatContext. Some formats do
 * not have a header or do not store enough information there, so it is
 * recommended that you call the avformat_find_stream_info() function which
 * tries to read and decode a few frames to find missing information.
 *
 * In some cases you might want to preallocate an AVFormatContext yourself
 * with avformat_alloc_context() and do some tweaking on it before passing it
 * to avformat_open_input(). One such case is when you want to use custom
 * functions for reading input data instead of lavf internal I/O layer. To
 * do that, create your own AVIOContext with avio_alloc_context(), passing your
 * reading callbacks to it. Then set the pb field of your AVFormatContext to
 * newly created AVIOContext.
 *
 * https://ffmpeg.org/doxygen/3.3/group__lavf__decoding.html#details
 */

// [3]
/**
 * This function populates pFormatCtx->streams with the proper information.
 *
 * Read packets of a media file to get stream information.
 * This is useful for file formats with no headers such as MPEG. This function
 * also computes the real framerate in case of MPEG-2 repeat frame mode. The
 * logical file position is not changed by this function; examined packets may
 * be buffered for later processing.
 *
 * Parameters:  AVFormatContext *   media file handle,
 *              AVDictionary **     If non-NULL, an ic.nb_streams long array of
 *                                  pointers to dictionaries, where i-th member
 *                                  contains options for codec corresponding to
 *                                  i-th stream. On return each dictionary will
 *                                  be filled with options that were not found.
 *
 * Returns >=0 if OK, AVERROR_xxx on error
 *
 * This function isn't guaranteed to open all the codecs, so options being
 * non-empty at return is a perfectly normal behavior.
 */

// [4]
/**
 * Print detailed information about the input or output format, such as
 * duration, bitrate, streams, container, programs, metadata, side data, codec
 * and time base.
 *
 * If av_dump_format works, but nb_streams is zero in your code, you have
 * mismatching libraries and headers I guess.
 *
 * av_dump_format() relies on nb_streams too, as can bee seen in its source.
 * So the binary code you used for av_dump_format can read nb_streams. It is
 * likely that you are using a precompiled static or dynamic avformat library,
 * which does not match your avformat.h header version. Thus your code may look
 * for nb_streams at an wrong location or type, for example.
 *
 * Make sure you use the header files exactly matching the ones used for making
 * the binaries of the libraries used.
 */

// [5]
/**
 * Other types for the encoded data include:
 * AVMEDIA_TYPE_UNKNOWN
 * AVMEDIA_TYPE_VIDEO
 * AVMEDIA_TYPE_AUDIO
 * AVMEDIA_TYPE_DATA
 * AVMEDIA_TYPE_SUBTITLE
 * AVMEDIA_TYPE_ATTACHMENT
 * AVMEDIA_TYPE_NB
 */

// [6]
/**
 * Find a registered decoder with a matching codec ID.
 */

// [7]
/**
 * Allocate an AVCodecContext and set its fields to default values.
 * The resulting struct should be freed with avcodec_free_context().
 *
 * Parameters: codec	if non-NULL, allocate private data and initialize
 *                      defaults for the given codec. It is illegal to then call
 *                      avcodec_open2() with a different codec. If NULL, then
 *                      the codec-specific defaults won't be initialized, which
 *                      may result in suboptimal default settings (this is
 *                      important mainly for encoders, e.g. libx264).
 *
 * Returns: An AVCodecContext filled with default values or NULL on failure.
 */

// [8]
/**
 * Initialize the AVCodecContext to use the given AVCodec.
 * Prior to using this function the context has to be allocated with
 * avcodec_alloc_context3().
 * The functions avcodec_find_decoder_by_name(), avcodec_find_encoder_by_name(),
 * avcodec_find_decoder() and avcodec_find_encoder() provide an easy way for
 * retrieving a codec.
 */

// [9]
/**
 * Allocate an AVFrame and set its fields to default values.
 * The resulting struct must be freed using av_frame_free().
 * Returns: An AVFrame filled with default values or NULL on failure.
 * Note: this only allocates the AVFrame itself, not the data buffers. Those
 * must be allocated through other means, e.g. with av_frame_get_buffer() or
 * manually.
 */

// [10]
/**
 * Return the size in bytes of the amount of data required to store an image
 * with the given parameters.
 */

// [11]
/**
 * av_malloc() is FFmpeg's malloc that is just a simple wrapper around malloc
 * that makes sure the memory addresses are aligned and such. It will not
 * protect you from memory leaks, double freeing, or other malloc problems.
 *
 * Allocate a block of size bytes with alignment suitable for all memory
 * accesses (including vectors if available on the CPU).
 *
 * Parameters: size	Size in bytes for the memory block to be allocated.
 *
 * Returns: Pointer to the allocated block, NULL if the block cannot be
 * allocated.
 */

// [12]
/**
 * Setup the picture fields based on the specified image parameters and the
 * provided image data buffer.
 * The picture fields are filled in by using the image data buffer pointed to
 * by ptr.
 * If ptr is NULL, the function will fill only the picture linesize array and
 * return the required size for the image buffer.
 * To allocate an image buffer and fill the picture data in one call, use
 * avpicture_alloc().
 *
 * Parameters:
 *  picture	the picture to be filled in
 *  ptr	buffer where the image data is stored, or NULL
 *  pix_fmt	the pixel format of the image
 *  width	the width of the image in pixels
 *  height	the height of the image in pixels
 */

// [13]
/**
 * Allocate and return an SwsContext.
 * You need it to perform scaling/conversion operations using sws_scale().
 */

// [14]
/**
 * Reading from an opened file:
 * Reading data from an opened AVFormatContext is done by repeatedly calling
 * av_read_frame() on it. Each call, if successful, will return an AVPacket
 * containing encoded data for one AVStream, identified by AVPacket.stream_index.
 * This packet may be passed straight into the libavcodec decoding functions
 * avcodec_decode_video2(), avcodec_decode_audio4() or avcodec_decode_subtitle2()
 * if the caller wishes to decode the data.
 *
 * AVPacket.pts, AVPacket.dts and AVPacket.duration timing information will be
 * set if known. They may also be unset (i.e. AV_NOPTS_VALUE for pts/dts, 0 for
 * duration) if the stream does not provide them. The timing information will be
 * in AVStream.time_base units, i.e. it has to be multiplied by the timebase to
 * convert them to seconds.
 * If AVPacket.buf is set on the returned packet, then the packet is allocated
 * dynamically and the user may keep it indefinitely. Otherwise, if AVPacket.buf
 * is NULL, the packet data is backed by a static storage somewhere inside the
 * demuxer and the packet is only valid until the next av_read_frame() call or
 * closing the file. If the caller requires a longer lifetime, av_dup_packet()
 * will make an av_malloc()ed copy of it. In both cases, the packet must be
 * freed with av_free_packet() when it is no longer needed.
 */
/**
 * Return the next frame of a stream.
 * This function returns what is stored in the file, and does not validate that
 * what is there are valid frames for the decoder. It will split what is stored
 * in the file into frames and return one for each call. It will not omit
 * invalid data between valid frames so as to give the decoder the maximum
 * information possible for decoding.
 * If pkt->buf is NULL, then the packet is valid until the next av_read_frame()
 * or until avformat_close_input(). Otherwise the packet is valid indefinitely.
 * In both cases the packet must be freed with av_free_packet when it is no
 * longer needed. For video, the packet contains exactly one frame. For audio,
 * it contains an integer number of frames if each frame has a known fixed size
 * (e.g. PCM or ADPCM data). If the audio frames have a variable size (e.g.
 * MPEG audio), then it contains one frame.
 * pkt->pts, pkt->dts and pkt->duration are always set to correct values in
 * AVStream.time_base units (and guessed if the format cannot provide them).
 * pkt->pts can be AV_NOPTS_VALUE if the video format has B-frames, so it is
 * better to rely on pkt->dts if you do not decompress the payload.
 */

// [15]
/**
 * The avcodec_send_packet()/avcodec_receive_frame()/avcodec_send_frame()/
 * avcodec_receive_packet() functions provide an encode/decode API, which
 * decouples input and output.
 *
 * https://www.ffmpeg.org/doxygen/4.0/group__lavc__encdec.html
 */

// [16]
/**
 * Scale the image slice in srcSlice and put the resulting scaled slice in the
 * image in dst.
 *
 * A slice is a sequence of consecutive rows in an image.
 *
 * Slices have to be provided in sequential order, either in top-bottom or
 * bottom-top order. If slices are provided in non-sequential order the
 * behavior of the function is undefined.
 *
 * Parameters
 *    c	        the scaling context previously created with sws_getContext()
 *    srcSlice	the array containing the pointers to the planes of the source
 *              slice
 *    srcStride	the array containing the strides for each plane of the source
 *              image
 *    srcSliceY	the position in the source image of the slice to process, that
 *              is the number (counted starting from zero) in the image of the
 *              first row of the slice
 *    srcSliceH	the height of the source slice, that is the number of rows in
 *              the slice
 *    dst       the array containing the pointers to the planes of the
 *              destination image
 *    dstStride	the array containing the strides for each plane of the
 *              destination image
 */

//  A note on packets
/**
 * Technically a packet can contain partial frames or other bits of data, but
 * ffmpeg's parser ensures that the packets we get contain either complete or
 * multiple frames.
 */

