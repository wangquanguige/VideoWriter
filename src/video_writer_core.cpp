#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <algorithm>
#include <dirent.h>
#include <fnmatch.h>
#include <functional>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/opt.h>
}

#include "video_writer_core.h"
#include <opencv2/core/core.hpp>

static size_t buffer_write(void *ptr, size_t size, size_t nmemb, MemoryBuffer *buffer) {
    size_t totalSize = size * nmemb;
    if(buffer->size + totalSize > buffer->capacity) {
        size_t new_capacity = buffer->capacity ==0 ? 1 : buffer->capacity * 2;
        while(new_capacity < buffer->size + totalSize) {
            new_capacity *= 2;
        }
        buffer->buffer = realloc(buffer->buffer, new_capacity);
        if(!buffer->buffer) {
            std::cerr << "Error: realloc error." << std::endl;
            exit(EXIT_FAILURE);
        }
        buffer->capacity = new_capacity;
    }

    memcpy((char *)buffer->buffer + buffer->size, ptr, totalSize);

    buffer->size += totalSize;
    
    return 0;
}

int32_t video_writer::encoder_yuv_to_h264(bool flushing) {
    int32_t result = 0;
    std::cout << "Send frame to encoder with pts:" << video_frame->pts << std::endl;

    result = avcodec_send_frame(video_codec_ctx, flushing ? nullptr : video_frame);
    if(result < 0) {
        std::cerr << "Error: avcodec_send_frame failed." << std::endl;
        return result;
    }
    while(result >= 0) {
        result = avcodec_receive_packet(video_codec_ctx, video_pkt);
        if(result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            return 1;
        }
        else if(result < 0) {
            std::cerr << "Error: avcodec_receive_packet failed." << std::endl;
            return result;
        }

        if(flushing) std::cout<<"Flushing: ";
        std::cout << "Got encoded packet with dts:" << video_pkt->dts << ", pts:" << video_pkt->pts << ", " << std::endl;
        buffer_write(video_pkt->data, 1, video_pkt->size, video_buffer);
    }
    return 0;
}

// 写入ADTS头
static void get_adts_header(AVCodecContext* ctx, uint8_t* adts_header, int aac_length)
{
    uint8_t freq_idx = 0;    //0: 96000 Hz  3: 48000 Hz 4: 44100 Hz
    switch (ctx->sample_rate) {
    case 96000: freq_idx = 0; break;
    case 88200: freq_idx = 1; break;
    case 64000: freq_idx = 2; break;
    case 48000: freq_idx = 3; break;
    case 44100: freq_idx = 4; break;
    case 32000: freq_idx = 5; break;
    case 24000: freq_idx = 6; break;
    case 22050: freq_idx = 7; break;
    case 16000: freq_idx = 8; break;
    case 12000: freq_idx = 9; break;
    case 11025: freq_idx = 10; break;
    case 8000: freq_idx = 11; break;
    case 7350: freq_idx = 12; break;
    default: freq_idx = 4; break;
    }
    uint8_t chanCfg = ctx->channels;
    uint32_t frame_length = aac_length + 7;
    adts_header[0] = 0xFF;
    adts_header[1] = 0xF1;
    adts_header[2] = ((ctx->profile) << 6) + (freq_idx << 2) + (chanCfg >> 2);
    adts_header[3] = (((chanCfg & 3) << 6) + (frame_length >> 11));
    adts_header[4] = ((frame_length & 0x7FF) >> 3);
    adts_header[5] = (((frame_length & 7) << 5) + 0x1F);
    adts_header[6] = 0xFC;
}

int32_t video_writer::encoder_pcm_to_aac(bool flushing) {
    int32_t result = 0;
    result = avcodec_send_frame(audio_codec_ctx, flushing ? nullptr : audio_frame);
    if(result < 0) {
        std::cerr << "Error: avcodec_send_frame failed." << std::endl;
        return result;
    }

    while(result >= 0) {
        result = avcodec_receive_packet(audio_codec_ctx, audio_pkt);
        if(result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            return 1;
        }
        else if(result < 0) {
            std::cerr << "Error: avcodec_receive_packet failed." << std::endl;
            return result;
        }
        if(flushing) {
            std::cout << "Flushing!" << std::endl;
        }

        uint8_t aac_header[7];
        get_adts_header(audio_codec_ctx, aac_header, audio_pkt->size);

        buffer_write(aac_header, 1, 7, audio_buffer);
        buffer_write(audio_pkt->data, 1, audio_pkt->size, audio_buffer);
    }
    return 0;
}

int32_t video_writer::cvmat_to_avframe(cv::Mat &inMat)
{
    // 得到Mat信息
    AVPixelFormat dstFormat = AV_PIX_FMT_YUV420P;
    int width = inMat.cols;
    int height = inMat.rows;







    video_frame->width = width;
    video_frame->height = height;
    video_frame->format = dstFormat;

    int ret = av_frame_get_buffer(video_frame, 0);
    if (ret < 0) 
    {
        std::cerr << "Could not allocate the video frame data." << std::endl;
        return -1; 
    }
    ret = av_frame_make_writable(video_frame);
    if(ret < 0) 
    {
        std::cerr << "Av frame make writable failed." << std::endl;
        return -1;
    }

    // 转换颜色空间为YUV420
    // cv::setNumThreads(1)
    cv::cvtColor(inMat, inMat, cv::COLOR_BGR2YUV_I420);

    // 按YUV420格式，设置数据地址
    int frame_size = width * height;
    unsigned char *data = inMat.data;
    memcpy(video_frame->data[0], data, frame_size);
    memcpy(video_frame->data[1], data + frame_size, frame_size / 4);
    memcpy(video_frame->data[2], data + frame_size * 5 / 4, frame_size / 4);

    video_frame->pts = frame_pts++;

    encoder_yuv_to_h264(false);

    av_frame_unref(video_frame);
    
    return 0;
}

void video_writer::flush() {
    encoder_yuv_to_h264(true);
}

video_writer::video_writer() {
    STREAM_FRAME_RATE = 25;
    frame_size = cv::Size(1280, 720);

    init();
    init_video_encoder();
    init_audio_encoder();
}

video_writer::video_writer(size_t frame_rate) {
    STREAM_FRAME_RATE = frame_rate;
    frame_size = cv::Size(1280, 720);

    init();
    init_video_encoder();
    init_audio_encoder();
}

video_writer::video_writer(cv::Size image_size) {
    STREAM_FRAME_RATE = 25;
    frame_size = image_size;

    init();
    init_video_encoder();
    init_audio_encoder();
}

video_writer::video_writer(size_t frame_rate, cv::Size image_size) {
    STREAM_FRAME_RATE = frame_rate;
    frame_size = image_size;

    init();
    init_video_encoder();
    init_audio_encoder();
}

int32_t video_writer::input_image(cv::Mat png_image) {
    cvmat_to_avframe(png_image);
    return 0;
}

int32_t video_writer::input_audio(char *audio_data, size_t size) {
    buffer_write(audio_data, 1, size, audio_buffer_to_encoder);

    size_t data_size = av_get_bytes_per_sample(audio_codec_ctx->sample_fmt);

    while(audio_buffer_to_encoder->size - audio_buffer_handled >= audio_frame->nb_samples * audio_codec_ctx->channels * data_size) {
        for(int i = 0; i < audio_frame->nb_samples; i++) {
            for(int ch = 0; ch < audio_codec_ctx->channels; ch++) {
                memcpy(audio_frame->data[ch] + data_size * i, (uint8_t *)audio_buffer_to_encoder->buffer + audio_buffer_handled, data_size);
                audio_buffer_handled += data_size;
            }
        }
        encoder_pcm_to_aac(false);
    }

    return 0;
}

video_writer::~video_writer() {
    free(video_buffer->buffer);
    free(audio_buffer->buffer);
    free(mux_buffer->buffer);
    free(audio_buffer_to_encoder->buffer);
    free(video_buffer);
    free(audio_buffer);
    free(mux_buffer);
    free(audio_buffer_to_encoder);

    if(video_codec_ctx) {
        avcodec_free_context(&video_codec_ctx);
    }
    if(audio_codec_ctx) {
        avcodec_free_context(&audio_codec_ctx);
    }

    if(video_frame) {
        av_frame_free(&video_frame);
    }
    if(audio_frame) {
        av_frame_free(&audio_frame);
    }

    if(video_pkt) {
        av_packet_free(&video_pkt);
    }
    if(audio_pkt) {
        av_packet_free(&audio_pkt);
    }


    if(video_fmt_ctx) {
        avformat_free_context(video_fmt_ctx);
    }
    if(audio_fmt_ctx) {
        avformat_free_context(audio_fmt_ctx);
    }

    if(output_fmt_ctx) {
        // output_fmt_ctx->pb是直接赋值的，不能调用avio_closep
        // avio_closep(&output_fmt_ctx->pb)
        // avformat_close_input(&output_fmt_ctx);
        avformat_free_context(output_fmt_ctx);
    }

    if(video_avio) {
        av_freep(&video_avio->buffer);
    }
    avio_context_free(&video_avio);

    if(audio_avio) {
        av_freep(&audio_avio->buffer);
    }
    avio_context_free(&audio_avio);

    if(mux_avio) {
        av_freep(&mux_avio->buffer);
    }
    avio_context_free(&mux_avio);
}

int32_t video_writer::init() {
    video_buffer = (MemoryBuffer *)malloc(sizeof(MemoryBuffer));
    audio_buffer = (MemoryBuffer *)malloc(sizeof(MemoryBuffer));
    mux_buffer = (MemoryBuffer *)malloc(sizeof(MemoryBuffer));
    audio_buffer_to_encoder = (MemoryBuffer *)malloc(sizeof(MemoryBuffer));

    video_buffer->buffer = nullptr;
    video_buffer->size = 0;
    video_buffer->capacity = 0;
    video_buffer->pos = 0;
    audio_buffer->buffer = nullptr;
    audio_buffer->size = 0;
    audio_buffer->capacity = 0;
    audio_buffer->pos = 0;
    mux_buffer->buffer = nullptr;
    mux_buffer->size = 0;
    mux_buffer->capacity = 0;
    mux_buffer->pos = 0;
    audio_buffer_to_encoder->buffer = nullptr;
    audio_buffer_to_encoder->size = 0;
    audio_buffer_to_encoder->capacity = 0;
    audio_buffer_to_encoder->pos = 0;

    video_pkt = av_packet_alloc();
    if(!video_pkt) {
        std::cerr << "Error: could not allocate video packet." << std::endl;
        return -1;
    }

    video_frame = av_frame_alloc();
    if(!video_frame) {
        std::cerr << "Error: failed to alloc frame." << std::endl;
        return -1;
    }
    return 0;
}

int32_t video_writer::init_video_encoder() {
    video_codec = avcodec_find_encoder_by_name("libx264");
    if(!video_codec) {
        std::cerr << "Error: could not find codec libx264." << std::endl;
        return -1;
    }

    video_codec_ctx = avcodec_alloc_context3(video_codec);
    if(!video_codec_ctx) {
        std::cerr << "Error: could not allocate video codec context." << std::endl;
        return -1;
    }

    video_codec_ctx->profile = FF_PROFILE_H264_HIGH;
    video_codec_ctx->bit_rate = 2000000;    // 输出码率




    video_codec_ctx->width = frame_size.width;
    video_codec_ctx->height = frame_size.height;
    video_codec_ctx->gop_size = 10;         // 关键帧间隔
    video_codec_ctx->time_base = (AVRational){1, STREAM_FRAME_RATE};
    video_codec_ctx->framerate = (AVRational){STREAM_FRAME_RATE, 1};
    video_codec_ctx->max_b_frames = 3;
    video_codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if(video_codec->id == AV_CODEC_ID_H264) {
        av_opt_set(video_codec_ctx->priv_data, "preset", "slow", 0);
    }

    // 初始化codec_ctx
    int32_t result = avcodec_open2(video_codec_ctx, video_codec, nullptr);
    if(result < 0) {
        std::cerr << "Error: could not open video codec." << std::endl;
        return -1;
    }

    return 1;
}

int32_t video_writer::init_audio_encoder() {
    audio_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if(!audio_codec) {
        std::cerr << "Error: could not find codec aac." << std::endl;
        return -1;
    }
    audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    if(!audio_codec_ctx) {
        std::cerr << "Error: could not allocate audio codec context." << std::endl;
        return -1;
    }

    audio_codec_ctx->bit_rate = 128000;                     // 输出码率
    audio_codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;       // 采样格式
    audio_codec_ctx->sample_rate = 44100;                   // 采样率
    audio_codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;  // 声道布局为立体声
    audio_codec_ctx->channels = 2;                          // 双声道

    int32_t result = avcodec_open2(audio_codec_ctx, audio_codec, nullptr);
    if(result < 0) {
        std::cerr << "Error: could not open audio codec." << std::endl;
        return -1;
    }

    audio_frame = av_frame_alloc();
    if(!audio_frame) {
        std::cerr << "Error: could not alloc frame." << std::endl;
        return -1;
    }

    audio_frame->nb_samples = audio_codec_ctx->frame_size;
    audio_frame->format = audio_codec_ctx->sample_fmt;
    audio_frame->channel_layout = audio_codec_ctx->channel_layout;
    result = av_frame_get_buffer(audio_frame, 0);
    if(result < 0) {
        std::cerr << "Error: AVFrame could not get buffer." << std::endl;
        return -1;
    }

    audio_pkt = av_packet_alloc();
    if(!audio_pkt) {
        std::cerr << "Error: could not alloc packet." << std::endl;
        return -1;
    }
    return -1;
}

static int video_read_buffer(void *opaque, uint8_t *buf, int buf_size) {
    MemoryBuffer *video_buffer = (MemoryBuffer *)opaque;
    if(video_buffer->pos >= video_buffer->size) {
        return -1;
    }

    memcpy(buf, (uint8_t *)video_buffer->buffer + video_buffer->pos, buf_size);

    if(video_buffer->pos + buf_size >= video_buffer->size) {
        int true_size = video_buffer->size - video_buffer->pos;
        video_buffer->pos = video_buffer->size;
        return true_size;
    }
    else {
        video_buffer->pos += buf_size;
        return buf_size;
    }
}

static int audio_read_buffer(void *opaque, uint8_t *buf, int buf_size) {
    MemoryBuffer *audio_buffer = (MemoryBuffer *)opaque;
    if(audio_buffer->pos >= audio_buffer->size) {
        return -1;
    }

    memcpy(buf, (uint8_t *)audio_buffer->buffer + audio_buffer->pos, buf_size);

    if(audio_buffer->pos + buf_size >= audio_buffer->size) {
        int true_size = audio_buffer->size - audio_buffer->pos;
        audio_buffer->pos = audio_buffer->size;
        return true_size;
    }
    else {
        audio_buffer->pos += buf_size;
        return buf_size;
    }
}

static int write_buffer(void *opaque, uint8_t *buf, int buf_size) {
    MemoryBuffer *mux_buffer = (MemoryBuffer *)opaque;
    return buffer_write(buf, 1, buf_size, mux_buffer);
}

int32_t video_writer::init_input_video() {
    int32_t result = 0;
    const AVInputFormat *video_input_format = av_find_input_format("h264");
    if(!video_input_format) {
        std::cerr << "Error: failed to find proper AVInputFormat for format h264." << std::endl;
        return -1;
    }
    video_fmt_ctx = avformat_alloc_context();
    if(!video_fmt_ctx) {
        std::cerr << "Error: could not alloc video_fmt_ctx." << std::endl;
        return -1;
    }

    // 以指定格式打开
    video_aviobuffer = (unsigned char *)av_malloc(32768);











    video_avio = avio_alloc_context(video_aviobuffer, 32768, 0, video_buffer,
                                    video_read_buffer, nullptr, nullptr);
    
    video_fmt_ctx->pb = video_avio;

    result = avformat_open_input(&video_fmt_ctx, nullptr, video_input_format, nullptr);
    if(result < 0) {
        std::cerr << "Error: video avformat_open_input failed!" << std::endl;
        return -1;
    }

    result = avformat_find_stream_info(video_fmt_ctx, nullptr);
    if(result < 0) {
        std::cerr << "Error: video avformat_find_stream_info failed!" << std::endl;
        return -1;
    }

    return result;
}

int32_t video_writer::init_input_audio() {
    int32_t result = 0;
    const AVInputFormat *audio_input_format = av_find_input_format("aac");
    if(!audio_input_format) {
        std::cerr << "Error: failed to find proper AVInputFormat for format aac." << std::endl;
        return -1;
    }

    audio_fmt_ctx = avformat_alloc_context();
    if(!audio_fmt_ctx) {
        std::cerr << "Error: could not alloc audio_fmt_ctx." << std::endl;
        return -1;
    }

    // 以指定格式打开
    audio_aviobuffer = (unsigned char *)av_malloc(32768);











    audio_avio = avio_alloc_context(audio_aviobuffer, 32768, 0, audio_buffer,
                                    audio_read_buffer, nullptr, nullptr);
    audio_fmt_ctx->pb = audio_avio;

    result = avformat_open_input(&audio_fmt_ctx, nullptr, audio_input_format, nullptr);
    if(result < 0) {
        std::cerr << result<<"Error: audio avformat_open_input failed!" << std::endl;
        return -1;
    }

    result = avformat_find_stream_info(audio_fmt_ctx, nullptr);
    if(result < 0) {
        std::cerr << "Error: audio avformat_find_stream_info failed!" << std::endl;
        return -1;
    }

    return result;
}

// write_packet时需要定位byte位置，此处为mux输出
static int64_t write_seek(void *opaque, int64_t offset, int whence) {
    MemoryBuffer *mux_buffer = (MemoryBuffer *)opaque;
    int64_t ret = -1;

    switch(whence) 
    {
        case AVSEEK_SIZE:
            ret = mux_buffer->capacity;
            break;
        case SEEK_SET:
            mux_buffer->size = offset;
            ret = (int64_t)((char *)mux_buffer->buffer + mux_buffer->size);
            break;
    }

    return ret;
}

int32_t video_writer::init_output() {
    int32_t result = 0;

    mux_aviobuffer = (unsigned char *)av_malloc(32768);

















    mux_avio = avio_alloc_context(mux_aviobuffer, 32768, 1, mux_buffer, nullptr,
                                  write_buffer, write_seek);

    avformat_alloc_output_context2(&output_fmt_ctx, nullptr, "mp4", nullptr);
    if(!output_fmt_ctx) {
        std::cerr << "Error: alloc output format context failed!" << std::endl;
        return -1;
    }

    output_fmt_ctx->pb = mux_avio;


    const AVOutputFormat *fmt = output_fmt_ctx->oformat;
    std::cout << "Default video codec id: " << fmt->video_codec << ", audio codec id: " << fmt->audio_codec << std::endl;

    AVStream *video_stream = avformat_new_stream(output_fmt_ctx, nullptr);
    if(!video_stream) {
        std::cerr << "Error: add video stream to output format context failed!" << std::endl;
        return -1;
    }

    out_video_st_idx = video_stream->index;
    in_video_st_idx = av_find_best_stream(video_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if(in_video_st_idx < 0) {
        std::cerr << "Error: find video stream in input video file failed!" << std::endl;
        return -1;
    }

    result = avcodec_parameters_copy(video_stream->codecpar, video_fmt_ctx->streams[in_video_st_idx]->codecpar);
    if(result < 0) {
        std::cerr << "Error: copy video codec parameters failed!" << std::endl;
        return -1;
    }

    video_stream->id = output_fmt_ctx->nb_streams - 1;
    video_stream->time_base = (AVRational){1, STREAM_FRAME_RATE};

    AVStream *audio_stream = avformat_new_stream(output_fmt_ctx, nullptr);
    if(!audio_stream) {
        std::cerr << "Error: add audio stream to output format context failed!" << std::endl;
        return -1;
    }

    out_audio_st_idx = audio_stream->index;
    in_audio_st_idx = av_find_best_stream(audio_fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if(in_audio_st_idx < 0) {
        std::cerr << "Error: find audio stream in input audio file failed!" << std::endl;
        return -1;
    }

    result = avcodec_parameters_copy(audio_stream->codecpar, audio_fmt_ctx->streams[in_audio_st_idx]->codecpar);
    if(result < 0) {
        std::cerr << "Error: copy audio codec parameters failed!" << std::endl;
        return -1;
    }

    audio_stream->id = output_fmt_ctx->nb_streams - 1;
    audio_stream->time_base = (AVRational){1, audio_stream->codecpar->sample_rate};

    /// av_dump_format(output_fmt_ctx, 0, output_file, 1);
    std::cout << "Output video idx: " << out_video_st_idx << ", audio idx: " << out_audio_st_idx << std::endl;











    return result;
}

int32_t video_writer::muxing() {
    int32_t result = 0;
    int64_t prev_video_dts = -1;
    int64_t cur_video_pts = 0, cur_audio_pts = 0;
    AVStream *in_video_st = video_fmt_ctx->streams[in_video_st_idx];
    AVStream *in_audio_st = audio_fmt_ctx->streams[in_audio_st_idx];
    AVStream *output_stream = nullptr, *input_stream = nullptr;

    int32_t video_frame_idx = 0;

    result = avformat_write_header(output_fmt_ctx, nullptr);
    if(result < 0) {
        return result;
    }

    av_init_packet(&muxer_pkt);
    muxer_pkt.data = nullptr;
    muxer_pkt.size = 0;

    std::cout << "Video r_frame_rate: " << in_video_st->r_frame_rate.num << "/" << in_video_st->r_frame_rate.den << std::endl;
    std::cout << "Video time_base: " << in_video_st->time_base.num << "/" << in_video_st->time_base.den << std::endl;

    // 循环写入音频包和视频包
    while(1) {
        if(av_compare_ts(cur_video_pts, in_video_st->time_base, cur_audio_pts, in_audio_st->time_base) <= 0) {
            // 视频包
            input_stream = in_video_st;
            result = av_read_frame(video_fmt_ctx, &muxer_pkt);
            if(result < 0) {
                av_packet_unref(&muxer_pkt);
                break;
            }

            if(muxer_pkt.pts == AV_NOPTS_VALUE) {
                int64_t frame_duration = (double)AV_TIME_BASE / av_q2d(in_video_st->r_frame_rate);
                muxer_pkt.duration = (double)frame_duration / (double)(av_q2d(in_video_st->time_base) * AV_TIME_BASE);
                muxer_pkt.pts = (double)(video_frame_idx * frame_duration) / (double)(av_q2d(in_video_st->time_base) * AV_TIME_BASE);
                muxer_pkt.dts = muxer_pkt.dts;
                std::cout << "frame_duration: " << frame_duration << ", muxer_pkt.duration: " << muxer_pkt.duration << ", muxer_pkt.pts: " << muxer_pkt.pts << std::endl;
            }

            video_frame_idx++;
            cur_video_pts = muxer_pkt.pts;
            muxer_pkt.stream_index = out_video_st_idx;
            output_stream = output_fmt_ctx->streams[out_video_st_idx];
        }
        else {
            // 音频包
            input_stream = in_audio_st;
            result = av_read_frame(audio_fmt_ctx, &muxer_pkt);
            if(result < 0) {
                av_packet_unref(&muxer_pkt);
                break;
            }

            cur_audio_pts = muxer_pkt.pts;
            muxer_pkt.stream_index = out_audio_st_idx;
            output_stream = output_fmt_ctx->streams[out_audio_st_idx];
        }

        muxer_pkt.pts = av_rescale_q_rnd(muxer_pkt.pts, input_stream->time_base, output_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        muxer_pkt.dts = av_rescale_q_rnd(muxer_pkt.dts, input_stream->time_base, output_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        muxer_pkt.duration = av_rescale_q(muxer_pkt.duration, input_stream->time_base, output_stream->time_base);
        std::cout << "Final muxer_pts: " << muxer_pkt.pts << ", duration: " << muxer_pkt.duration << ", output_stream->time_base: " << output_stream->time_base.num << "/" << output_stream->time_base.den << std::endl;

        if(av_interleaved_write_frame(output_fmt_ctx, &muxer_pkt) < 0) {
            std::cerr << "Error: failed to mux packet!" << std::endl;
            break;
        }
        av_packet_unref(&muxer_pkt);
    }

    result = av_write_trailer(output_fmt_ctx);
    if(result < 0) {
        return result;
    }

    return result;
}

int32_t video_writer::video_mux() {
    int32_t result = init_input_video();
    if(result < 0) {
        return result;
    }

    result = init_input_audio();
    if(result < 0) {
        return result;
    }

    result = init_output();
    if(result < 0) {
        return result;
    }

    result = muxing();
    if(result < 0) {
        return result;
    }
    
    return 1;
}

int32_t video_writer::write_h264(char *output_file) {
    FILE *outputFile = fopen(output_file, "wb");
    fwrite(video_buffer->buffer, 1, video_buffer->size, outputFile);
    if(outputFile != nullptr) {
        fclose(outputFile);
        outputFile = nullptr;
    }
    return 0;
}

int32_t video_writer::write_aac(char *output_file) {
    FILE *outputFile = fopen(output_file, "wb");
    fwrite(audio_buffer->buffer, 1, audio_buffer->size, outputFile);
    if(outputFile != nullptr) {
        fclose(outputFile);
        outputFile = nullptr;
    }
    return 0;
}

int32_t video_writer::write_video(char *output_file) {
    FILE *outputFile = fopen(output_file, "wb");
    fwrite(mux_buffer->buffer, 1, mux_buffer->size, outputFile);
    if(outputFile != nullptr) {
        fclose(outputFile);
        outputFile = nullptr;
    }
    return 0;
}