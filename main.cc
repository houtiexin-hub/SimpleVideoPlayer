#include <iostream>
extern "C" {
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libswscale/swscale.h"
    #include "libavutil/imgutils.h"
}

#define SDL_MAIN_HANDLED

#include "SDL.h"

class DealSDL {
private:
    SDL_Window* sdl_windows_ = nullptr;
    SDL_Renderer* sdl_render_ = nullptr;
    SDL_Texture* sdl_texture_ = nullptr;

public:
    explicit DealSDL() = default;
    ~DealSDL() {
        Release();
    }

    void Release() {
        if (sdl_texture_) {
            SDL_DestroyTexture(sdl_texture_);
            sdl_texture_ = nullptr;
        }

        if (sdl_render_) {
            SDL_DestroyRenderer(sdl_render_);
            sdl_render_ = nullptr;
        }

        if (sdl_windows_) {
            SDL_DestroyWindow(sdl_windows_);
            sdl_windows_ = nullptr;
        }

        SDL_Quit();
    }

    int Init() {
        //初始化SDL SDL_INIT_TIMER 时间相关的功能和事件 SDL_INIT_VIDEO 窗口管理 图形渲染和视频显示
        int ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
        if (0 != ret) {
            std::cerr << "SDL_Init fail" << std::endl;
            return ret;
        }
        //创建一个窗体 程序运行会弹出该窗体 
        sdl_windows_ = SDL_CreateWindow("SimpleVideoPlayer", 0, 0, 1920, 1080, SDL_WINDOW_OPENGL);
        if (nullptr == sdl_windows_) {
            std::cerr << "SDL_CreateWindow fail" << std::endl;
            return ret;
        }
        //创建一个渲染器
        sdl_render_ = SDL_CreateRenderer(sdl_windows_, -1, SDL_RENDERER_ACCELERATED);
        if (nullptr == sdl_render_) {
            std::cerr << "SDL_CreateRenderer fail" << std::endl;
            return ret;
        }

        return 0;
    }

    int CreateTexture(const int& width, const int& height) {
        //创建一个纹理，纹理宽高要与待播放视频宽高相同
        sdl_texture_ = SDL_CreateTexture(sdl_render_, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (nullptr == sdl_texture_) {
            std::cerr << "SDL_CreateTexture fail" << std::endl;
            return -1;
        }
        
        return 0;
    }

    void Play(AVFrame* frame) {
        //将帧数据拷贝到纹理
        SDL_UpdateYUVTexture(sdl_texture_, nullptr,
            frame->data[0], frame->linesize[0],
            frame->data[1], frame->linesize[1],
            frame->data[2], frame->linesize[2]);
        //清空渲染目标，保证渲染目标是干净的
        SDL_RenderClear(sdl_render_);
        //将纹理对象拷贝到渲染目标
        SDL_RenderCopy(sdl_render_, sdl_texture_, nullptr, nullptr);
        //执行渲染
        SDL_RenderPresent(sdl_render_);
        //定时指定整个解码、渲染操作（没有按照视频原始帧率）
        SDL_Delay(40);
    }
};

class DecoderVideo {
private:
    AVFormatContext* format_ctx_ = nullptr;
    int video_stream_index_ = -1;
    AVCodecContext* video_codec_ctx_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;
    AVFrame* raw_frame_ = nullptr;
    AVFrame* i420_frame_ = nullptr;
    DealSDL sdl_;

public:
    explicit DecoderVideo() = default;
    ~DecoderVideo() {
        Release();
    }

    void Release() {
        if (i420_frame_) {
            av_frame_free(&i420_frame_);
            i420_frame_ = nullptr;
        }

        if (raw_frame_) {
            av_frame_free(&raw_frame_);
            raw_frame_ = nullptr;
        }

        if (packet_) {
            av_packet_free(&packet_);
            packet_ = nullptr;
        }

        if (sws_ctx_) {
            sws_freeContext(sws_ctx_);
            sws_ctx_ = nullptr;
        }

        if (video_codec_ctx_) {
            avcodec_free_context(&video_codec_ctx_);
            video_codec_ctx_ = nullptr;
        }

        if (format_ctx_) {
            avformat_close_input(&format_ctx_);
            avformat_free_context(format_ctx_);
            format_ctx_ = nullptr;
        }
    }

    int Init(const char* file_name) {
        //解封装上下文申请空间
        format_ctx_ = avformat_alloc_context();
        //通过评分判断文件的封装格式或者传输协议，获取基本信息比如：帧率、时长、时基
        int ret = avformat_open_input(&format_ctx_, file_name, nullptr, nullptr);
        if (ret < 0) {
            std::cerr << "avformat_open_input fail" << std::endl;
            return ret;
        }
        //通过解码一些数据获取流信息
        ret = avformat_find_stream_info(format_ctx_, nullptr);
        if (ret < 0) {
            std::cerr << "avformat_find_stream_info fail" << std::endl;
            return ret;
        }
        //拿到流类型对应的stream_index
        video_stream_index_ = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (video_stream_index_ < 0) {
            std::cerr << "av_find_best_stream fail" << std::endl;
            return ret;
        }

        //查看是否有对应的解码器被注册
        auto video_decodec = avcodec_find_decoder(format_ctx_->streams[video_stream_index_]->codecpar->codec_id);
        if (nullptr == video_decodec) {
            std::cerr << "avcodec_find_decoder fail" << std::endl;
            return ret;
        }
        //申请解码器上下文，为指定解码器分配私有数据并初始化默认值
        video_codec_ctx_ = avcodec_alloc_context3(video_decodec);
        if (nullptr == video_codec_ctx_)
        {
            std::cerr << "avcodec_alloc_context3 fail" << std::endl;
            return ret;
        }
        //拷贝源素材的编码信息到解码器上下文
        ret = avcodec_parameters_to_context(video_codec_ctx_, format_ctx_->streams[video_stream_index_]->codecpar);
        if (ret < 0) {
            std::cerr << "avcodec_parameters_to_context fail" << std::endl;
            return ret;
        }
        //打开特定的编解码器，并将其关联到给定的 AVCodecContext 上下文中
        ret = avcodec_open2(video_codec_ctx_, video_decodec, nullptr);
        if (ret < 0)
        {
            std::cerr << "avcodec_open2 fail" << std::endl;
            return ret;
        }
        //色彩格式转换的初始化（帧数据更改为SDL纹理支持的格式）
        sws_ctx_ = sws_getContext(video_codec_ctx_->width, video_codec_ctx_->height, video_codec_ctx_->pix_fmt,
            video_codec_ctx_->width, video_codec_ctx_->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC,
            nullptr, nullptr, nullptr);
        raw_frame_ = av_frame_alloc();
        i420_frame_ = av_frame_alloc();
        i420_frame_->width = video_codec_ctx_->width;
        i420_frame_->height = video_codec_ctx_->height;
        i420_frame_->format = AV_PIX_FMT_YUV420P;
        av_frame_get_buffer(i420_frame_, 1);
        packet_ = av_packet_alloc();    
        ret = sdl_.Init();
        if (ret != 0) {
            return ret;
        }

        ret = sdl_.CreateTexture(video_codec_ctx_->width, video_codec_ctx_->height);
        if (ret != 0) {
            return ret;
        }

        return ret;
    }

    int Decoder() {
        //从流里面读取packet（编码后的数据）
        //对于视频来说，一个packet只包含一个frame
        //对于音频来说，若是帧长固定的格式则一个packet可包含整数个frame，
        //              若是帧长可变的格式则一个packet只包含一个frame
        while (av_read_frame(format_ctx_, packet_) == 0) {
            //只处理视频流
            if (packet_->stream_index != video_stream_index_) {
                continue;
            }
            //向解码器抛入packet数据
            int ret = avcodec_send_packet(video_codec_ctx_, packet_);
            if (ret == 0) {
                while (ret >= 0) {
                    //拿到帧数据去渲染展示
                    ret = avcodec_receive_frame(video_codec_ctx_, raw_frame_);
                    if (ret == AVERROR(EAGAIN)) {
                        break;
                    }
                    else if (ret == AVERROR_EOF) {
                        return 0;
                    }
                    else if (ret < 0) {
                        return -1;
                    }

                    sws_scale(sws_ctx_, raw_frame_->data, raw_frame_->linesize, 0, raw_frame_->height, i420_frame_->data, i420_frame_->linesize);
                    sdl_.Play(i420_frame_);
                }
            }

            av_packet_unref(packet_);
        }

        return 0;
    }
};

int main() {
    DecoderVideo decode_video;
    int ret = decode_video.Init("D:/ffmpeg_tool/rgb.mov");
    if (ret != 0) {
        return ret;
    }

    decode_video.Decoder();
    return 0;
}