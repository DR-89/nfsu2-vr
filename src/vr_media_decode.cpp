#include "vr_media_decode.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {
constexpr AVRational clock_base={1,vr_media_rate};

/* Independent cursors keep a poorly interleaved MP4 from buffering an entire
 * stream. All demuxing, decoding and scaling runs on the playlist worker. */
struct Stream {
    AVFormatContext *file=nullptr;
    AVCodecContext *codec=nullptr;
    AVPacket *packet=av_packet_alloc();
    AVFrame *frame=av_frame_alloc();
    AVStream *stream=nullptr;
    bool draining=false;

    ~Stream() {
        av_frame_free(&frame);av_packet_free(&packet);
        avcodec_free_context(&codec);avformat_close_input(&file);
    }
    int open(const std::filesystem::path &path,AVMediaType type) {
        if(!packet || !frame)return AVERROR(ENOMEM);
        int rc=avformat_open_input(&file,path.u8string().c_str(),nullptr,nullptr);
        if(rc<0 || (rc=avformat_find_stream_info(file,nullptr))<0)return rc;
        const AVCodec *decoder=nullptr;
        int index=av_find_best_stream(file,type,-1,-1,&decoder,0);
        if(index<0)return index;
        stream=file->streams[index];
        if(!(codec=avcodec_alloc_context3(decoder)))return AVERROR(ENOMEM);
        if((rc=avcodec_parameters_to_context(codec,stream->codecpar))<0)return rc;
        codec->pkt_timebase=stream->time_base;
        codec->max_pixels=4096*2304;
        return avcodec_open2(codec,decoder,nullptr);
    }
    int next() {
        for(;;) {
            int rc=avcodec_receive_frame(codec,frame);
            if(rc!=AVERROR(EAGAIN))return rc;
            if(draining)return AVERROR_EOF;
            do {
                av_packet_unref(packet);
                rc=av_read_frame(file,packet);
            } while(rc>=0 && packet->stream_index!=stream->index);
            if(rc<0 && rc!=AVERROR_EOF)return rc;
            draining=rc==AVERROR_EOF;
            rc=avcodec_send_packet(codec,draining?nullptr:packet);
            av_packet_unref(packet);
            if(rc<0)return rc;
        }
    }
    int64_t time(int64_t fallback) const {
        if(frame->best_effort_timestamp==AV_NOPTS_VALUE)return fallback;
        int64_t origin=file->start_time==AV_NOPTS_VALUE?0:
            av_rescale_q(file->start_time,AV_TIME_BASE_Q,clock_base);
        return std::max<int64_t>(0,av_rescale_q(frame->best_effort_timestamp,stream->time_base,clock_base)-origin);
    }
};

struct Audio {
    Stream source;
    SwrContext *resampler=nullptr;
    AVChannelLayout layout{};
    int format=AV_SAMPLE_FMT_NONE,rate=0;
    std::vector<int16_t> pcm;
    int64_t cursor=0,lead=0;
    int offset=0,available=0;
    bool ended=true,align_start=false;

    ~Audio() {av_channel_layout_uninit(&layout);swr_free(&resampler);}
    bool refill() {
        int rc=source.next();
        if(rc<0 && rc!=AVERROR_EOF)return false;
        const AVFrame *input=rc==0?source.frame:nullptr;
        if(!resampler) {
            if(!input || input->sample_rate<=0 || av_channel_layout_copy(&layout,&input->ch_layout)<0)return false;
            format=input->format;rate=input->sample_rate;
            if(align_start)lead=source.time(0);
            AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
            if(swr_alloc_set_opts2(&resampler,&stereo,AV_SAMPLE_FMT_S16,vr_media_rate,
                &layout,static_cast<AVSampleFormat>(format),rate,0,nullptr)<0 || swr_init(resampler)<0)return false;
        } else if(input && (input->format!=format || input->sample_rate!=rate ||
                            av_channel_layout_compare(&layout,&input->ch_layout)))return false;
        int frames=input?input->nb_samples:0;
        int capacity=swr_get_out_samples(resampler,frames);
        if(capacity<0 || capacity>1024*1024)return false;
        pcm.resize(2u*std::max(1,capacity));offset=0;
        auto out=reinterpret_cast<uint8_t *>(pcm.data());
        available=swr_convert(resampler,&out,capacity,input?(const uint8_t **)input->extended_data:nullptr,frames);
        ended=!input && available==0;
        return available>=0;
    }
    /* Advance to a presentation timestamp in stereo frames. Silence uses the
     * same mixer clock for silent video, delayed audio and a longer picture. */
    bool until(unsigned id,const QuestRadioSong &song,int64_t end,bool pad) {
        static const int16_t silence[2048]={};
        while(cursor<end) {
            if(!available && !lead && !ended) {
                if(!refill())return false;
                continue;
            }
            if(!available && !lead && !pad)break;
            bool quiet=lead || !available;
            int64_t chunk=std::min<int64_t>(end-cursor,quiet?1024:available);
            if(lead)chunk=std::min(chunk,lead);
            if(!vr_buffer_audio(id,song,quiet?silence:pcm.data()+2*offset,static_cast<size_t>(chunk)))return false;
            if(lead)lead-=chunk;
            else if(!quiet){offset+=static_cast<int>(chunk);available-=static_cast<int>(chunk);}
            cursor+=chunk;
        }
        return true;
    }
};

struct Screen {
    SwsContext *scaler=nullptr;
    std::vector<uint32_t> scaled;
    ~Screen(){sws_freeContext(scaler);}

    std::shared_ptr<VrVideoPixels> picture(const Stream &video,int turns) {
        AVFrame *in=video.frame;
        AVRational sar=av_guess_sample_aspect_ratio(video.file,video.stream,in);
        double ratio=(double)in->width/in->height*(sar.num>0 && sar.den>0?av_q2d(sar):1.);
        int max_w=turns%2?VR_VIDEO_HEIGHT:VR_VIDEO_WIDTH,max_h=turns%2?VR_VIDEO_WIDTH:VR_VIDEO_HEIGHT;
        int w=std::clamp<int>(int(std::round(std::min<double>(max_w,max_h*ratio))),1,max_w);
        int h=std::clamp<int>(int(std::round(std::min<double>(max_h,max_w/ratio))),1,max_h);
        int stride=(w+15)&~15;
        scaled.resize(stride*h+16);
        uint8_t *dst[4]={reinterpret_cast<uint8_t *>(scaled.data())};
        int lines[4]={4*stride};
        scaler=sws_getCachedContext(scaler,in->width,in->height,static_cast<AVPixelFormat>(in->format),
            w,h,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
        const int *coefficients=sws_getCoefficients(in->colorspace);
        if(!scaler || sws_setColorspaceDetails(scaler,coefficients,in->color_range==AVCOL_RANGE_JPEG,
            coefficients,1,0,1<<16,1<<16)<0 ||
            sws_scale(scaler,in->data,in->linesize,0,in->height,dst,lines)!=h)return {};
        auto pixels=std::make_shared<VrVideoPixels>();
        int dx=(VR_VIDEO_WIDTH-(turns%2?h:w))/2,dy=(VR_VIDEO_HEIGHT-(turns%2?w:h))/2;
        for(int y=0;y<h;++y) {
            const uint32_t *row=scaled.data()+y*stride;
            if(!turns){std::copy_n(row,w,pixels->data()+(dy+y)*VR_VIDEO_WIDTH+dx);continue;}
            for(int x=0;x<w;++x) {
                int col=turns==1?h-1-y:turns==2?w-1-x:y;
                int line=turns==1?x:turns==2?h-1-y:w-1-x;
                (*pixels)[(dy+line)*VR_VIDEO_WIDTH+dx+col]=row[x];
            }
        }
        return pixels;
    }
};

bool video_track(Stream &video,Audio &audio,unsigned id,const QuestRadioSong &song) {
    auto params=video.stream->codecpar;
    const auto matrix=av_packet_side_data_get(params->coded_side_data,params->nb_coded_side_data,AV_PKT_DATA_DISPLAYMATRIX);
    double rotation=matrix && matrix->size>=9*sizeof(int32_t)?
        -av_display_rotation_get(reinterpret_cast<const int32_t *>(matrix->data)):0.;
    int turns=std::isfinite(rotation)?(4+int(std::lround(rotation/90.)))%4:0;
    AVRational fps=av_guess_frame_rate(video.file,video.stream,nullptr);
    int64_t typical=fps.num>0 && fps.den>0?av_rescale_q(1,av_inv_q(fps),clock_base):vr_media_rate/30;
    int64_t end=0,next_picture=0;
    Screen screen;
    int rc;
    while((rc=video.next())==0) {
        int64_t at=video.time(end);
        int64_t duration=av_rescale_q(video.frame->duration,video.stream->time_base,clock_base);
        end=at+std::max<int64_t>(1,duration>0?duration:typical);
        if(at<next_picture)continue;
        if(!audio.until(id,song,at,true))return false;
        auto pixels=screen.picture(video,turns);
        if(!pixels || !vr_buffer_picture(id,at,std::move(pixels)))return false;
        next_picture=at+vr_media_rate/30-1;
    }
    if(rc!=AVERROR_EOF)return false;
    if(!end)return !audio.ended;
    // End the image on the same playback clock. An AAC tail can then drive
    // the visualizer while the rest of the file continues to play.
    return audio.until(id,song,end,true) && vr_buffer_picture(id,end,nullptr);
}
} // namespace

bool vr_decode_track(const std::filesystem::path &path,unsigned id) {
    std::string ext=path.extension().u8string();
    for(char &c:ext)if(c>='A' && c<='Z')c+=32;
    Stream video;
    bool movie=ext==".mp4" && video.open(path,AVMEDIA_TYPE_VIDEO)>=0 &&
        video.stream->codecpar->codec_id==AV_CODEC_ID_H264 && !(video.stream->disposition&AV_DISPOSITION_ATTACHED_PIC);
    Audio audio;
    if(!movie || av_find_best_stream(video.file,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0)>=0) {
        if(audio.source.open(path,AVMEDIA_TYPE_AUDIO)<0)return false;
        audio.ended=false;audio.align_start=movie;
    }
    AVFormatContext *file=movie?video.file:audio.source.file;
    auto title=av_dict_get(file->metadata,"title",nullptr,0),artist=av_dict_get(file->metadata,"artist",nullptr,0);
    QuestRadioSong info{};
    std::snprintf(info.title,sizeof info.title,"%s",title && *title->value?title->value:path.stem().u8string().c_str());
    std::snprintf(info.artist,sizeof info.artist,"%s",artist?artist->value:movie?"Custom Video":"Custom Music");
    if(movie && !video_track(video,audio,id,info))return false;
    return audio.until(id,info,INT64_MAX,false) && audio.cursor>0;
}
