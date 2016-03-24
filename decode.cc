extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx;
static AVStream *video_stream = NULL, *audio_stream = NULL;
static const char *src_filename = NULL;
static FILE *video_dst_file = NULL;
static FILE *audio_dst_file = NULL;

static int video_stream_idx = -1, audio_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket pkt;
static int video_frame_count = 0;
static int audio_frame_count = 0;

static uint8_t* video_buffer = nullptr;
static int video_buffer_size = 0;

static int decode_packet(int *got_frame, int cached)
{
  int ret = 0;
  int decoded = pkt.size;

  *got_frame = 0;

  if (pkt.stream_index == video_stream_idx) {
    /* decode video frame */
    ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, &pkt);
    if (ret < 0) {
      fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
      return ret;
    }

    if (*got_frame) {
      fwrite(&frame->width, 1, sizeof(frame->width), video_dst_file);
      fwrite(&frame->height, 1, sizeof(frame->height), video_dst_file);
      fwrite(&frame->format, 1, sizeof(frame->format), video_dst_file);
      if (!video_buffer) {
	video_buffer_size = av_image_get_buffer_size(AVPixelFormat(frame->format),
						     frame->width,
						     frame->height,
						     1);
	video_buffer = (uint8_t*) malloc(video_buffer_size);
      }
      int written = av_image_copy_to_buffer(video_buffer, video_buffer_size,
					    frame->data, frame->linesize,
					    AVPixelFormat(frame->format),
					    frame->width,
					    frame->height,
					    1);
      printf("%d %d\n", written, video_buffer_size);
      //fwrite(video_dst_data[0], 1, video_dst_bufsize, video_dst_file);
    }
  } else if (pkt.stream_index == audio_stream_idx) {
    /* decode audio frame */
    ret = avcodec_decode_audio4(audio_dec_ctx, frame, got_frame, &pkt);
    if (ret < 0) {
      fprintf(stderr, "Error decoding audio frame (%s)\n", av_err2str(ret));
      return ret;
    }
    /* Some audio decoders decode only part of the packet, and have to be
     * called again with the remainder of the packet data.
     * Sample: fate-suite/lossless-audio/luckynight-partial.shn
     * Also, some decoders might over-read the packet. */
    decoded = FFMIN(ret, pkt.size);

    if (*got_frame) {
      //size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(AVSampleFormat(frame->format));
#if 0
      printf("audio_frame%s n:%d nb_samples:%d pts:%s\n",
	     cached ? "(cached)" : "",
	     audio_frame_count++, frame->nb_samples,
	     av_ts2timestr(frame->pts, &audio_dec_ctx->time_base));
#endif

      /* Write the raw audio data samples of the first plane. This works
       * fine for packed formats (e.g. AV_SAMPLE_FMT_S16). However,
       * most audio decoders output planar audio, which uses a separate
       * plane of audio samples for each channel (e.g. AV_SAMPLE_FMT_S16P).
       * In other words, this code will write only the first audio channel
       * in these cases.
       * You should use libswresample or libavfilter to convert the frame
       * to packed data. */
      //fwrite(frame->extended_data[0], 1, unpadded_linesize, audio_dst_file);
    }
  }

  return decoded;
}

static int open_codec_context(int *stream_idx,
                              AVFormatContext *fmt_ctx, enum AVMediaType type)
{
  int ret, stream_index;
  AVStream *st;
  AVCodecContext *dec_ctx = NULL;
  AVCodec *dec = NULL;
  AVDictionary *opts = NULL;

  ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
  if (ret < 0) {
    fprintf(stderr, "Could not find %s stream in input file '%s'\n",
	    av_get_media_type_string(type), src_filename);
    return ret;
  } else {
    stream_index = ret;
    st = fmt_ctx->streams[stream_index];

    /* find decoder for the stream */
    dec_ctx = st->codec;
    dec = avcodec_find_decoder(dec_ctx->codec_id);
    if (!dec) {
      fprintf(stderr, "Failed to find %s codec\n",
	      av_get_media_type_string(type));
      return AVERROR(EINVAL);
    }

    /* Init the decoders, without reference counting */
    av_dict_set(&opts, "refcounted_frames", "0", 0);
    if ((ret = avcodec_open2(dec_ctx, dec, &opts)) < 0) {
      fprintf(stderr, "Failed to open %s codec\n",
	      av_get_media_type_string(type));
      return ret;
    }
    *stream_idx = stream_index;
  }

  return 0;
}

int main (int argc, char **argv)
{
  int ret = 0, got_frame;

  if (argc != 2) {
    fprintf(stderr,
	    "usage: %s <video-file>\n"
	    "This will dump video frames to stdout and audio frames to stderr.\n"
	    , argv[0]);
    exit(-1);
  }
  src_filename = argv[1];

  /* register all formats and codecs */
  av_register_all();

  /* open input file, and allocate format context */
  if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
    fprintf(stderr, "Could not open source file %s\n", src_filename);
    exit(1);
  }

  /* retrieve stream information */
  if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
    fprintf(stderr, "Could not find stream information\n");
    exit(1);
  }

  if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
    video_stream = fmt_ctx->streams[video_stream_idx];
    video_dec_ctx = video_stream->codec;
    video_dst_file = stdout;
  }

  if (open_codec_context(&audio_stream_idx, fmt_ctx, AVMEDIA_TYPE_AUDIO) >= 0) {
    audio_stream = fmt_ctx->streams[audio_stream_idx];
    audio_dec_ctx = audio_stream->codec;
    audio_dst_file = stderr;
  }

  /* dump input information to stderr */
  av_dump_format(fmt_ctx, 0, src_filename, 0);

  if (!audio_stream && !video_stream) {
    fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
    ret = 1;
    goto end;
  }

  frame = av_frame_alloc();
  if (!frame) {
    fprintf(stderr, "Could not allocate frame\n");
    ret = AVERROR(ENOMEM);
    goto end;
  }

  /* initialize packet, set data to NULL, let the demuxer fill it */
  av_init_packet(&pkt);
  pkt.data = NULL;
  pkt.size = 0;

  /* read frames from the file */
  while (av_read_frame(fmt_ctx, &pkt) >= 0) {
    AVPacket orig_pkt = pkt;
    do {
      ret = decode_packet(&got_frame, 0);
      if (ret < 0)
	break;
      pkt.data += ret;
      pkt.size -= ret;
    } while (pkt.size > 0);
    av_packet_unref(&orig_pkt);
  }

  /* flush cached frames */
  pkt.data = NULL;
  pkt.size = 0;
  do {
    decode_packet(&got_frame, 1);
  } while (got_frame);

  printf("Demuxing succeeded.\n");

  if (audio_stream) {
    enum AVSampleFormat sfmt = audio_dec_ctx->sample_fmt;
    int n_channels = audio_dec_ctx->channels;
    const char *fmt;

    if (av_sample_fmt_is_planar(sfmt)) {
      const char *packed = av_get_sample_fmt_name(sfmt);
      fprintf(stderr,
	      "Warning: the sample format the decoder produced is planar "
	      "(%s). This example will output the first channel only.\n",
	     packed ? packed : "?");
      sfmt = av_get_packed_sample_fmt(sfmt);
      n_channels = 1;
    }

    /*
    printf("Play the output audio file with the command:\n"
	   "ffplay -f %s -ac %d -ar %d %s\n",
	   fmt, n_channels, audio_dec_ctx->sample_rate,
	   audio_dst_filename);
    */
  }

 end:
  avcodec_close(video_dec_ctx);
  avcodec_close(audio_dec_ctx);
  avformat_close_input(&fmt_ctx);
  if (video_dst_file)
    fclose(video_dst_file);
  if (audio_dst_file)
    fclose(audio_dst_file);
  av_frame_free(&frame);

  return ret < 0;
}
