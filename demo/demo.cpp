#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <string>
#include <algorithm>
#include <dirent.h>
#include <fnmatch.h>
#include <time.h>

#include <opencv2/imgcodecs.hpp>
#include "video_writer_core.h"

static void usage(const char *program_name) {
    std::cout << "usage: " << std::string(program_name) << " png_file_dir pcm_file output_file" << std::endl;
}

template<typename T>
std::string to_string(T value) {
    std::stringstream ss;
    ss << value;
    return ss.str();
}

int main(int argc, char **argv) {
    video_writer writer(25, cv::Size(1280, 720));
    //video_writer writer(25, cv::Size(854, 480));

    char png_file_dir[] = "../test";
    std::vector<std::string>png_files;
    std::string file_path(png_file_dir);

    DIR *dir = opendir(png_file_dir);
    if(dir == nullptr) {
        std::cerr << "Error: could not open input file dir " << png_file_dir << std::endl;
        return 0;
    }

    struct dirent *entry;
    while((entry = readdir(dir)) != nullptr) {
        if(fnmatch("*.png", entry->d_name, 0) == 0) {
            png_files.push_back(file_path + "/" + entry->d_name);
        }
    }
    closedir(dir);

    // 按文件名排序
    std::sort(png_files.begin(), png_files.end());

    clock_t image_time = 0, audio_time = 0, mux_time = 0;
    clock_t image_start = 0, image_end = 0;

    for(int i = 0; i < png_files.size(); i++) {
    //for(int i = 0; i < 10; i++) {
        cv::Mat image = cv::imread(png_files[i]);

        image_start = clock();
        writer.input_image(image);
        image_end = clock();
        image_time += image_end - image_start;
    }

    // 刷新编码器，表示Mat输入流结束
    writer.flush();

    FILE *file = fopen("../test.pcm", "rb");
    if(file == nullptr) {
        std::cerr << "Error: could not open input pcm file: test.pcm." << std::endl;
        return 0;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    int64_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // 计算每次读取的字节数
    int64_t chunk_size = file_size / 10;
    // 分配内存
    char *buffer = (char *)malloc(chunk_size);
    if(buffer == nullptr) {
        std::cerr << "Error: could not alloc pcm buffer." << std::endl;
        fclose(file);
        return 0;
    }

    clock_t audio_start = 0, audio_end = 0;
    int64_t read_size = 0;
    // 多次读取pcm文件
    while(read_size < file_size) {
        if(read_size + chunk_size > file_size) {
            chunk_size = file_size - read_size;
        }

        fread(buffer, 1, chunk_size, file);
        read_size += chunk_size;

        audio_start = clock();
        writer.input_audio(buffer, chunk_size);
        audio_end = clock();
        audio_time += audio_end - audio_start;
    }

    free(buffer);
    fclose(file);

    clock_t mux_start = 0, mux_end = 0;
    mux_start = clock();
    // 封装MP4视频需要手动调用接口
    writer.video_mux();
    mux_end = clock();
    mux_time = mux_end - mux_start;

    writer.write_h264("test.h264");
    writer.write_aac("test.aac");
    writer.write_video("test.mp4");

    std::cout << "帧数量：" << png_files.size() << std::endl;
    std::cout << "音频字节：" << file_size << std::endl;
    std::cout << "image_time = " << double(image_time) / CLOCKS_PER_SEC << "s" << std::endl;
    std::cout << "audio_time = " << double(audio_time) / CLOCKS_PER_SEC << "s" << std::endl;
    std::cout << "mux_time = " << double(mux_time) / CLOCKS_PER_SEC << "s" << std::endl;

    return 0;
}
