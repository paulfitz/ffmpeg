#include <iostream>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

AVFrame* videoFrame = nullptr;
AVCodecContext* cctx = nullptr;
SwsContext* swsCtx = nullptr;
int frameCounter = 0;
AVFormatContext* ofctx = nullptr;
const AVOutputFormat* oformat = nullptr;
AVStream* stream = nullptr;
int fps = 20;
int width = 400; // 1920;
int height = 300; // 1080;
int bitrate = 2000;
int depth = 3;

static void pushFrame(uint8_t* data, double t)
{
    int err;
    if (!videoFrame) {
        videoFrame = av_frame_alloc();
        videoFrame->format = AV_PIX_FMT_YUV420P;
        videoFrame->width = cctx->width;
        videoFrame->height = cctx->height;

        if ((err = av_frame_get_buffer(videoFrame, 32)) < 0) {
          printf("Failed to allocate picture\n");
          return;
        }
    }

    if (!swsCtx) {
      swsCtx = sws_getContext(cctx->width, cctx->height, (depth == 3) ? AV_PIX_FMT_RGB24 : AV_PIX_FMT_RGBA, cctx->width, cctx->height,
                                AV_PIX_FMT_YUV420P, SWS_BICUBIC, 0, 0, 0);
    }

    int inLinesize[1] = {4 * cctx->width};

    // From RGB to YUV
    sws_scale(swsCtx, (const uint8_t* const*)&data, inLinesize, 0, cctx->height, videoFrame->data,
              videoFrame->linesize);
//90k
    if (t > 0) {
      videoFrame->pts = t * stream->time_base.den / (stream->time_base.num);
    } else {
      videoFrame->pts = (frameCounter++) * stream->time_base.den / (stream->time_base.num * fps);
    }

    //videoFrame->pts = (1.0/fps) * 1000 * (frameCounter++);

    //std::cout << videoFrame->pts << " " << cctx->time_base.num << " " << cctx->time_base.den << " " << frameCounter
      //        << std::endl;

    if ((err = avcodec_send_frame(cctx, videoFrame)) < 0) {
      printf("Failed to send frame\n");
      return;
    }
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    pkt.flags |= AV_PKT_FLAG_KEY;
    int ret = 0;
    if ((ret = avcodec_receive_packet(cctx, &pkt)) == 0) {
        static int counter = 0;
        printf("pkt key and stuff\n");
        //std::cout << "pkt key: " << (pkt.flags & AV_PKT_FLAG_KEY) << " " << pkt.size << " " << (counter++) << std::endl;
        uint8_t* size = ((uint8_t*)pkt.data);
        //std::cout << "first: " << (int)size[0] << " " << (int)size[1] << " " << (int)size[2] << " " << (int)size[3]
        //<< " " << (int)size[4] << " " << (int)size[5] << " " << (int)size[6] << " " << (int)size[7]
        //<< std::endl;

        av_interleaved_write_frame(ofctx, &pkt);
    }
    // std::cout << "push: " << ret << std::endl;
    printf("push: %d\n", ret);
    av_packet_unref(&pkt);
}

static void finish()
{
    // DELAYED FRAMES
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    for (;;) {
        avcodec_send_frame(cctx, NULL);
        if (avcodec_receive_packet(cctx, &pkt) == 0) {
            av_interleaved_write_frame(ofctx, &pkt);
            // std::cout << "final push: " << std::endl;
            printf("Final push\n");
        } else {
            break;
        }
    }

    av_packet_unref(&pkt);

    av_write_trailer(ofctx);
    if (!(oformat->flags & AVFMT_NOFILE)) {
        int err = avio_close(ofctx->pb);
        if (err < 0) {
          //std::cout << "Failed to close file" << err << std::endl;
          printf("Failed to close file\n");
        }
    }
}

static void xfree()
{
    if (videoFrame) {
        av_frame_free(&videoFrame);
        videoFrame = NULL;
    }

    if (cctx) {
        avcodec_free_context(&cctx);
        cctx = NULL;
    }
    if (ofctx) {
        avformat_free_context(ofctx);
        ofctx = NULL;
    }
    if (swsCtx) {
        sws_freeContext(swsCtx);
        swsCtx = NULL;
    }
}

extern "C" int testing2(int argc, char* argv[]);

int testing2(int argc, char* argv[])
{
  //av_register_all();
  //avcodec_register_all();

    const char *fname = "test.mp4"; //"test.webm";
    oformat = av_guess_format(nullptr, fname, nullptr);
    if (!oformat) {
      //std::cout << "can't create output format" << std::endl;
      printf("cannot create output format\n");
      return -1;
    }
    //oformat->video_codec = AV_CODEC_ID_H265;

    int err = avformat_alloc_output_context2(&ofctx, (AVOutputFormat *)oformat, nullptr, fname);

    if (err) {
      printf("cannot create output context\n");
      //std::cout << "can't create output context" << std::endl;
      return -1;
    }

    const AVCodec* codec = nullptr;

    codec = avcodec_find_encoder(oformat->video_codec);
    if (!codec) {
      // std::cout << "can't create codec" << std::endl;
      printf("cannot create codec\n");
      return -1;
    }

    stream = avformat_new_stream(ofctx, codec);

    if (!stream) {
      //std::cout << "can't find format" << std::endl;
      printf("cannot find format\n");
      return -1;
    }

    cctx = avcodec_alloc_context3(codec);

    if (!cctx) {
      //std::cout << "can't create codec context" << std::endl;
      printf("cannot create codec context\n");
      return -1;
    }

    stream->codecpar->codec_id = oformat->video_codec;
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->width = width;
    stream->codecpar->height = height;
    stream->codecpar->format = AV_PIX_FMT_YUV420P;
    stream->codecpar->bit_rate = bitrate * 1000;
    stream->avg_frame_rate = (AVRational){fps, 1};
    avcodec_parameters_to_context(cctx, stream->codecpar);
    cctx->time_base = (AVRational){1, 1};
    cctx->max_b_frames = 2;
    cctx->gop_size = 12;
    cctx->framerate = (AVRational){fps, 1};

    if (stream->codecpar->codec_id == AV_CODEC_ID_H264) {
        av_opt_set(cctx, "preset", "ultrafast", 0);
    } else if (stream->codecpar->codec_id == AV_CODEC_ID_H265) {
        av_opt_set(cctx, "preset", "ultrafast", 0);
    }
    else
    {
        av_opt_set_int(cctx, "lossless", 1, 0);
    }

    avcodec_parameters_from_context(stream->codecpar, cctx);

    if ((err = avcodec_open2(cctx, codec, NULL)) < 0) {
      //std::cout << "Failed to open codec" << err << std::endl;
      printf("Failed to open codec\n");
      return -1;
    }

    if (!(oformat->flags & AVFMT_NOFILE)) {
        if ((err = avio_open(&ofctx->pb, fname, AVIO_FLAG_WRITE)) < 0) {
          // std::cout << "Failed to open file" << err << std::endl;
          printf("Failed to open file\n");
          return -1;
        }
    }

    if ((err = avformat_write_header(ofctx, NULL)) < 0) {
      //std::cout << "Failed to write header" << err << std::endl;
      printf("Failed to write header\n");
      return -1;
    }

    av_dump_format(ofctx, 0, fname, 1);
    // std::cout << stream->time_base.den  << " " << stream->time_base.num << std::endl;
    printf("time %g %g\n", (double) stream->time_base.den, (double) stream->time_base.num);

    int w = width; // 1920;
    int h = height; // 1080;
    uint8_t* frameraw = (uint8_t *) malloc(w * h * 4);
     // new uint8_t[1920 * 1080 * 4];
    int at = 0;
    for (int i = 0; i < 5; ++i) {
      memset(frameraw, at, w * h * 4);
      memset(frameraw, 255 - at, w * h * 2);
      pushFrame(frameraw, 0);
      at += 20;
        usleep(500000);
    }

    // delete[] frameraw;
    free(frameraw);
    frameraw = NULL;
    finish();
    xfree();
    return 0;
}

extern "C" int maker_start(int w, int h, int d, const char *fname);
extern "C" int maker_add(int w, int h, int d, int len, uint8_t *frameraw, double t);
extern "C" int maker_stop();

// int at = 0;
int started = 0;
extern "C" int maker_add(int w, int h, int d, int len, uint8_t *frameraw, double t) {
  if (!started) {
    maker_start(w, h, d, "video.mp4");
  }
  printf("Add %dx%dx%d\n", w, h, d);
  if (len != w * h * d || w != width || h != height || d != depth) {
    printf("dimensions are wrong\n");
    return 1;
  }
  //  int w = width; // 1920;
  // int h = height; // 1080;
  // uint8_t* frameraw = (uint8_t *) malloc(w * h * 4);
  // new uint8_t[1920 * 1080 * 4];
  // memset(frameraw, at, w * h * 4);
  // memset(frameraw, 255 - at, w * h * 2);
  pushFrame(frameraw, t);
  // at += 20;
  // usleep(500000);
  // free(frameraw);
  // frameraw = NULL;
  return 0;
}

extern "C" int maker_stop() {
  if (started) {
    finish();
    xfree();
  }
  started = 0;
  return 0;  
}

extern "C" int maker_start(int w, int h, int d, const char *fname) {
  maker_stop();
  width = w;
  height = h;
  depth = d;
  started = 1;
  oformat = av_guess_format(nullptr, fname, nullptr);
  if (!oformat) {
    //std::cout << "can't create output format" << std::endl;
    printf("cannot create output format\n");
    return -1;
  }
  //oformat->video_codec = AV_CODEC_ID_H265;

  int err = avformat_alloc_output_context2(&ofctx, (AVOutputFormat *)oformat, nullptr, fname);

  if (err) {
    printf("cannot create output context\n");
    //std::cout << "can't create output context" << std::endl;
    return -1;
  }

  const AVCodec* codec = nullptr;

  codec = avcodec_find_encoder(oformat->video_codec);
  if (!codec) {
    // std::cout << "can't create codec" << std::endl;
    printf("cannot create codec\n");
    return -1;
  }

  stream = avformat_new_stream(ofctx, codec);

  if (!stream) {
    //std::cout << "can't find format" << std::endl;
    printf("cannot find format\n");
    return -1;
  }

  cctx = avcodec_alloc_context3(codec);

  if (!cctx) {
    //std::cout << "can't create codec context" << std::endl;
    printf("cannot create codec context\n");
    return -1;
  }

  stream->codecpar->codec_id = oformat->video_codec;
  stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
  stream->codecpar->width = width;
  stream->codecpar->height = height;
  stream->codecpar->format = AV_PIX_FMT_YUV420P;
  stream->codecpar->bit_rate = bitrate * 1000;
  stream->avg_frame_rate = (AVRational){fps, 1};
  avcodec_parameters_to_context(cctx, stream->codecpar);
  cctx->time_base = (AVRational){1, 1};
  cctx->max_b_frames = 2;
  cctx->gop_size = 12;
  cctx->framerate = (AVRational){fps, 1};
  // cctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  if (stream->codecpar->codec_id == AV_CODEC_ID_H264) {
    av_opt_set(cctx, "preset", "ultrafast", 0);
  } else if (stream->codecpar->codec_id == AV_CODEC_ID_H265) {
    av_opt_set(cctx, "preset", "ultrafast", 0);
  }
  else
    {
      av_opt_set_int(cctx, "lossless", 1, 0);
    }

  avcodec_parameters_from_context(stream->codecpar, cctx);
  // cctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  if ((err = avcodec_open2(cctx, codec, NULL)) < 0) {
    //std::cout << "Failed to open codec" << err << std::endl;
    printf("Failed to open codec\n");
    return -1;
  }

  if (!(oformat->flags & AVFMT_NOFILE)) {
    if ((err = avio_open(&ofctx->pb, fname, AVIO_FLAG_WRITE)) < 0) {
      // std::cout << "Failed to open file" << err << std::endl;
      printf("Failed to open file\n");
      return -1;
    }
  }

  if ((err = avformat_write_header(ofctx, NULL)) < 0) {
    //std::cout << "Failed to write header" << err << std::endl;
    printf("Failed to write header\n");
    return -1;
  }

  av_dump_format(ofctx, 0, fname, 1);
  // std::cout << stream->time_base.den  << " " << stream->time_base.num << std::endl;
  printf("time %g %g\n", (double) stream->time_base.den, (double) stream->time_base.num);
  return 0;
}


#include <stdio.h>
//#include "cmdutils.h"

extern "C" {
  extern const char program_name[] = "ffmkswt";
  extern const int program_birth_year = 2021;
}

extern "C" void show_help_default(const char *opt, const char *arg);

void show_help_default(const char *opt, const char *arg) {
  printf("nah\n");
}

extern "C" int testing1();

extern "C" int testing1() {
  printf("What up\n");
  return 42;
}


//extern "C" int testing2(int x);

//extern "C" int testing2(int x) {
//  printf("What up? %d\n", x);
//  return x + 1;
//}

extern "C" int main(int argc, char *argv[]) {
  printf("yo\n");
  if (argc > 3) {
    /*
    FILE *fp = fopen("hello.txt", "r");
    if (fp != NULL) {
      printf("find file\n");
      char ch;
      while((ch = fgetc(fp)) != EOF)
        printf("%c", ch);
      
      fclose(fp);
      const char *v[] = {"wot", "wot.mp4"};
      testing2(2, (char **)v);
    } else {
      printf("Could not find file\n");
    }
    */
    maker_start(400, 300, 4, "wot2.mp4");
    double t = 0;
    for (int i=0; i<100; i++) {
      uint8_t *buf = (uint8_t *)malloc(400*300*4);
      int at = 0;
      for (int y = 0; y<300; y++) {
        for (int x = 0; x<400; x++) {
          buf[at] = (x%13) ? 255 : 0;
          at++;
          buf[at] = (i%4 >= 2) ? 0 : 0;
          at++;
          buf[at] = (i%8 >= 4) ? 0 : 0;
          at++;
          buf[at] = (i%8 >= 4) ? 0 : 0;
          at++;
        }
      }
      maker_add(400, 300, 4, 400 * 300 * 4, buf, t);
      t += 0.5;
      free(buf);
    }
    maker_stop();
  }
  return 0;
}
