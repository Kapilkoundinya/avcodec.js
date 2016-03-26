extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <unistd.h>

static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx;
static AVStream *video_stream = NULL, *audio_stream = NULL;
static const char *src_filename = NULL;

static int video_stream_idx = -1, audio_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket pkt;

static uint8_t* video_buffer = nullptr;
static int video_buffer_size = 0;

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
      if (!video_buffer) {
	video_buffer_size = av_image_get_buffer_size(AVPixelFormat(frame->format),
						     frame->width,
						     frame->height,
						     1);
	video_buffer = (uint8_t*) malloc(video_buffer_size);
      }
      int bytes = av_image_copy_to_buffer(video_buffer, video_buffer_size,
					  frame->data, frame->linesize,
					  AVPixelFormat(frame->format),
					  frame->width,
					  frame->height,
					  1);
      bytes += 20;
      send(&bytes);
      send(&video_tag);
      send(&frame->width);
      send(&frame->height);
      send(&frame->format);
      send(video_buffer, video_buffer_size);
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
      send(&frame->channels);
      send(&frame->format);
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
  av_log_set_level(AV_LOG_QUIET);

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

 end:
  avcodec_close(video_dec_ctx);
  avcodec_close(audio_dec_ctx);
  avformat_close_input(&fmt_ctx);
  av_frame_free(&frame);

  return ret < 0;
}
