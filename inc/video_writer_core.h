#ifndef VIDEO_WRITER_CORE_H
#define VIDEO_WRITER_CORE_H
#include <stdint.h>
#include <vector>
#include <string>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
}

#include <opencv2/core/core.hpp>
#include <opencv2/opencv.hpp>

typedef struct {
    void *buffer;         // buffer已存储大小
    size_t size;          // buffer的容量
    size_t capacity;      // 待处理的字节索引
    size_t pos;
}MemoryBuffer;

class video_writer {
    private:
        int STREAM_FRAME_RATE;
        // 编码音频视频数据存储
        MemoryBuffer *video_buffer;
        MemoryBuffer *audio_buffer;
        MemoryBuffer *mux_buffer;

        //编码器尺寸，默认Size(1280, 720)
        cv::Size frame_size;

        AVBufferRef *hw_device_ctx= NULL;

        // 输入音频数据存储
        MemoryBuffer *audio_buffer_to_encoder;
        size_t audio_buffer_handled = 0;

        size_t frame_pts = 0;

        AVFrame *video_frame = NULL;
        AVFrame *audio_frame = NULL;
        AVPacket *video_pkt, *audio_pkt;
        AVCodec *video_codec = NULL;
        AVCodec *audio_codec = NULL;
        AVCodecContext *video_codec_ctx = NULL;
        AVCodecContext *audio_codec_ctx = NULL;

        AVIOContext *video_avio = NULL;
        AVIOContext *audio_avio = NULL;
        AVIOContext *mux_avio = NULL;
        unsigned char *video_aviobuffer;
        unsigned char *audio_aviobuffer;
        unsigned char *mux_aviobuffer;

        AVFormatContext *video_fmt_ctx = NULL, *audio_fmt_ctx = NULL, output_fmt_ctx = NULL;
        AVPacket muxer_pkt;
        int32_t in_video_st_idx = -1, in_audio_st_idx = -1;
        int32_t out_video_st_idx = -1, out_video_st_idx = -1;

        int32_t cvmat_to_avframe(cv::Mat &inMat);
        int32_t writer_frame_to_yuv();
        int32_t encoder_yuv_to_h264(bool flushing);
        int32_t encoder_pcm_to_aac(bool flushing);
        void get_adts_header(AVCodecContext* ctx, unit8_t* adts_header, int aac_length);
        int32_t muxing();

        int32_t init_video_encoder();
        int32_t init_audio_encoder();
        int32_t init_input_video();
        int32_t init_input_audio();
        int32_t init_output();
        int32_t init();









    public:
        video_writer();
        video_writer(size_t frame_rate);
        video_writer(cv:Size image_size);
        video_writer(size_t frame_size, cv:Size image_size);

        // 输入帧Mat数据
        int32_t input_image(cv::Mat png_image);
        // 输入音频char *数据
        int32_t input_audio(char *audio_data, size_t size);

        // 执行mux操作
        int32_t video_mux();

        // 输出h264视频文件
        int32_t write_h264(char *output_file);
        // 输出aac音频文件
        int32_t write_aac(char *output_file);
        // 输出MP4音视频文件
        int32_t write_video(char *output_file);

        // 刷新编码器，表示输入流的结束
        // 如未刷新编码器可能会有packet残留，输出视频不完整
        void flush()

        ~video_writer();
};

#endif