extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libavutil/log.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <unistd.h>

static AVFormatContext *fmt_ctx = nullptr;
static AVCodecContext *video_dec_ctx = nullptr, *audio_dec_ctx;
static AVStream *video_stream = nullptr, *audio_stream = nullptr;
static const char *src_filename = nullptr;
static const char *color_space = nullptr;

static int video_stream_idx = -1, audio_stream_idx = -1;
static AVFrame *frame = nullptr;
static AVPacket pkt;

static uint8_t* video_pointers[4] = { nullptr, };
static int video_linesizes[4] = { 0, };
static int video_buffer_size = 0;
static AVPixelFormat video_output_format = AV_PIX_FMT_RGB24;

static SwsContext *sws_ctx = nullptr;

static int video_tag = 0x22057601;
static int audio_tag = 0x22057602;

static void send(void *ptr, int bytes)
{
  write(STDOUT_FILENO, ptr, bytes);
}

template <typename T>
static void send(T *value) {
  send(value, sizeof(T));
}

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
      if (!video_buffer_size)
	video_buffer_size = av_image_alloc(video_pointers, video_linesizes,
					   frame->width, frame->height, video_output_format, 1);
      if (!sws_ctx) {
	sws_ctx = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format),
				 frame->width, frame->height, video_output_format,
				 SWS_BILINEAR, nullptr, nullptr, nullptr);
      }
      // https://ffmpeg.org/pipermail/libav-user/2013-May/004715.html
      AVRational frame_rate = video_stream->r_frame_rate;
      if (video_stream->codec->codec_id == AV_CODEC_ID_H264 ) {
        frame_rate = video_stream->avg_frame_rate;
      }
      double fps = frame_rate.num / (double) frame_rate.den;
      unsigned int nb_frames = (unsigned int) video_stream->nb_frames;
      sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height, video_pointers, video_linesizes);
      int bytes = video_buffer_size;
      bytes += 32;
      send(&bytes);
      send(&video_tag);
      send(&video_output_format);
      send(&frame->width);
      send(&frame->height);
      send(&nb_frames);
      send(&fps);
      send(video_pointers[0], video_buffer_size);
    }
  } else if (pkt.stream_index == audio_stream_idx) {
    /* decode audio frame */
    ret = avcodec_decode_audio4(audio_dec_ctx, frame, got_frame, &pkt);
    if (ret < 0) {
      fprintf(stderr, "Error decoding audio frame (%s)\n", av_err2str(ret));
      return ret;
    }

    decoded = FFMIN(ret, pkt.size);

    if (*got_frame) {
      int unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(AVSampleFormat(frame->format));
      int bytes = unpadded_linesize * frame->channels;
      bytes += 20;
      send(&bytes);
      send(&audio_tag);
      send(&frame->format);
      send(&frame->channels);
      send(&frame->nb_samples);
      for (int c = 0; c < frame->channels; ++c)
	send(frame->extended_data[c], unpadded_linesize);
    }
  }

  return decoded;
}

static int open_codec_context(int *stream_idx,
                              AVFormatContext *fmt_ctx, enum AVMediaType type)
{
  int ret, stream_index;
  AVStream *st;
  AVCodecContext *dec_ctx = nullptr;
  AVCodec *dec = nullptr;
  AVDictionary *opts = nullptr;

  ret = av_find_best_stream(fmt_ctx, type, -1, -1, nullptr, 0);
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

  if (argc != 3) {
    fprintf(stderr,
	    "usage: %s <video-file> <color-space>\n"
	    "This will dump video and audio frames to stdout using a simple protocol.\n"
	    "color-space can be 'rgb' or 'yuv420p'.\n"
	    , argv[0]);
    exit(-1);
  }
  src_filename = argv[1];
  color_space = argv[2];

  if (!strcmp(color_space, "yub420p"))
    video_output_format = AV_PIX_FMT_YUV420P;
  else if (!strcmp(color_space, "bgr"))
    video_output_format = AV_PIX_FMT_BGR24;

  /* register all formats and codecs */
  av_register_all();
  av_log_set_level(AV_LOG_QUIET);

  /* open input file, and allocate format context */
  if (avformat_open_input(&fmt_ctx, src_filename, nullptr, nullptr) < 0) {
    fprintf(stderr, "Could not open source file %s\n", src_filename);
    exit(1);
  }

  /* retrieve stream information */
  if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
    fprintf(stderr, "Could not find stream information\n");
    exit(1);
  }

  if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
    video_stream = fmt_ctx->streams[video_stream_idx];
    video_dec_ctx = video_stream->codec;
  }

  if (open_codec_context(&audio_stream_idx, fmt_ctx, AVMEDIA_TYPE_AUDIO) >= 0) {
    audio_stream = fmt_ctx->streams[audio_stream_idx];
    audio_dec_ctx = audio_stream->codec;
  }

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

  /* initialize packet, set data to nullptr, let the demuxer fill it */
  av_init_packet(&pkt);
  pkt.data = nullptr;
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
  pkt.data = nullptr;
  pkt.size = 0;
  do {
    decode_packet(&got_frame, 1);
  } while (got_frame);

 end:
  avcodec_close(video_dec_ctx);
  avcodec_close(audio_dec_ctx);
  avformat_close_input(&fmt_ctx);
  av_frame_free(&frame);

  return ret < 0;
}
