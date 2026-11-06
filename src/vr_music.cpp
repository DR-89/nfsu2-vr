#include "vr_media_decode.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <deque>
#include <mutex>
#include <random>
#include <thread>
#include <vector>
#include <SDL_timer.h>
extern "C" {
#include <libavutil/tx.h>
}
namespace {
namespace fs = std::filesystem;

constexpr size_t fft_frames=1024;
using SpectrumChannel=std::array<float,fft_frames>;
using SpectrumPcm=std::array<SpectrumChannel,2>;

struct ChannelHistory {
    SpectrumChannel pcm{};
    uint64_t frames=0;
    uint32_t tick=0;

    template<class Sample>
    void append(const Sample *input,size_t count,size_t stride,uint32_t now) {
        for(size_t i=count>fft_frames?count-fft_frames:0;i<count;++i)
            pcm[(frames+i)%fft_frames]=input[i*stride];
        frames+=count;tick=now;
    }
    bool ready(uint32_t now) const { return frames>=fft_frames && now-tick<160; }
    void snapshot(SpectrumChannel &out,uint32_t now) const {
        if(!ready(now)){out.fill(0);return;}
        auto oldest=pcm.begin()+frames%fft_frames;
        auto next=std::copy(oldest,pcm.end(),out.begin());
        std::copy(pcm.begin(),oldest,next);
    }
};

struct SpectrumHistory {
    std::mutex mutex;
    ChannelHistory channels[2];
    uint64_t epoch=0;
    std::atomic<uint64_t> stock_voices{0};
    int stock_track=-1;
    uint32_t stock_tick=0;

    void reset_locked() {
        channels[0].frames=channels[1].frames=0;
        ++epoch;
    }
    void clear_custom() {
        std::lock_guard<std::mutex> lock(mutex);
        if(!stock_voices)reset_locked();
    }
    // The renderer can miss a sample window without holding up audio output.
    void capture_custom(const int16_t *a,size_t na,const int16_t *b,size_t nb) {
        std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);
        if(!lock || stock_voices)return;
        uint32_t now=SDL_GetTicks();
        for(int ch=0;ch<2;++ch) {
            if(na>=2)channels[ch].append(a+ch,na/2,2,now);
            if(nb>=2)channels[ch].append(b+ch,nb/2,2,now);
        }
    }
} history;

struct Picture {
    int64_t at;
    uint64_t serial;
    std::shared_ptr<const VrVideoPixels> pixels;
};

struct Player {
    std::vector<fs::path> tracks;
    size_t index = 0;
    unsigned generation = 0;
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool stopping = false, ready = false, paused = false;
    int source = QUEST_MUSIC_AUTO;
    std::atomic<bool> scanning_or_ready{false}, playing{false};
    uint32_t heartbeat = 0;
    float volume = 0;
    QuestRadioSong song{};
    std::array<int16_t, 32768> ring{};
    size_t head = 0, count = 0;

    std::deque<Picture> pictures;
    std::atomic<bool> has_picture{false};
    bool screen_active=false;
    int64_t played=0;
    uint64_t serial=0;

    void reset_timeline() {
        pictures.clear();has_picture=false;played=0;
        history.clear_custom();
    }
    bool enqueue(unsigned id,const QuestRadioSong &info,const int16_t *pcm,size_t samples) {
        std::unique_lock<std::mutex> lock(mutex);
        while(samples) {
            wake.wait(lock,[&]{return stopping || id!=generation || count<ring.size();});
            if(stopping || id!=generation)return false;
            size_t tail=(head+count)%ring.size();
            size_t n=std::min({samples,ring.size()-count,ring.size()-tail});
            std::copy_n(pcm,n,ring.data()+tail);
            pcm+=n;count+=n;samples-=n;
            song=info;ready=true;
        }
        return true;
    }
} player;

/* One window per channel at 48 kHz. Keeping the channels separate preserves
 * side-panned and opposite-phase signals. All FFT work stays on the renderer. */
struct Spectrum {
    AVTXContext *context=nullptr;
    av_tx_fn transform=nullptr;
    std::array<float,fft_frames> window{};
    std::array<float,QUEST_RADIO_BANDS> levels{};
    float beat_average=0,pulse=0;
    uint64_t epoch=0,seen=0;

    Spectrum() {
        float scale=1.f;
        av_tx_init(&context,&transform,AV_TX_FLOAT_RDFT,0,fft_frames,&scale,AV_TX_UNALIGNED);
        for(size_t i=0;i<fft_frames;++i)
            window[i]=float((.5-.5*std::cos(6.283185307179586*i/fft_frames))*(4./(32768.*fft_frames)));
    }
    ~Spectrum(){av_tx_uninit(&context);}
    void measure(const SpectrumPcm &pcm,float seconds) {
        if(!context)return;
        // FFT bins are 46.875 Hz apart. Widen the bands toward the treble.
        constexpr int edges[]={1,2,3,4,6,8,11,16,22,31,44,62,87,123,174,246,385};
        static_assert(sizeof edges/sizeof *edges==QUEST_RADIO_BANDS+1);
        std::array<float,QUEST_RADIO_BANDS> power{};
        std::array<float,fft_frames> input;
        std::array<AVComplexFloat,fft_frames/2+1> bins;
        for(int ch=0;ch<2;++ch) {
            for(size_t i=0;i<fft_frames;++i)input[i]=pcm[ch][i]*window[i];
            transform(context,bins.data(),input.data(),sizeof(float));
            for(int band=0;band<QUEST_RADIO_BANDS;++band)
                for(int bin=edges[band];bin<edges[band+1];++bin) {
                    const auto &z=bins[bin];
                    power[band]=std::max(power[band],z.re*z.re+z.im*z.im);
                }
        }
        for(int band=0;band<QUEST_RADIO_BANDS;++band) {
            float db=10.f*std::log10(std::max(power[band],1.e-6f));
            float height=std::clamp((db+54.f)/54.f,0.f,1.f);
            levels[band]=std::max(height,levels[band]-seconds*3.f);
        }
        // Bass attacks lead the lights. Mids and treble still pick up a snare
        // in a track without bass. A sustained note settles into the average.
        float bass=*std::max_element(power.begin(),power.begin()+4);
        float mids=*std::max_element(power.begin()+4,power.begin()+10);
        float treble=*std::max_element(power.begin()+10,power.end());
        float energy=std::sqrt(std::max({bass,.22f*mids,.07f*treble}));
        float attack=std::clamp((energy-1.12f*beat_average)/(.07f+beat_average),0.f,1.f);
        pulse=std::max(attack*std::min(1.f,6.f*energy),pulse*std::exp(-seconds/.13f));
        beat_average+=(energy-beat_average)*(-std::expm1(-seconds/.24f));
    }
};

void run(fs::path folder) {
    std::error_code ec;
    fs::create_directories(folder,ec);
    std::vector<fs::path> tracks;
    for(fs::directory_iterator it(folder,ec),end;!ec && it!=end;it.increment(ec)) {
        std::error_code type_error;
        if(!it->is_regular_file(type_error))continue;
        auto ext=it->path().extension().u8string();
        for(char &c:ext)if(c>='A' && c<='Z')c+=32;
        if(ext==".mp3" || ext==".flac" || ext==".wav" || ext==".mp4")tracks.push_back(it->path());
    }
    if(ec)std::fprintf(stderr,"[Music] Cannot scan %s: %s\n",folder.u8string().c_str(),ec.message().c_str());
    std::mt19937 random(SDL_GetPerformanceCounter());
    std::shuffle(tracks.begin(),tracks.end(),random);
    std::unique_lock<std::mutex> lock(player.mutex);
    player.tracks=std::move(tracks);
    while(!player.stopping && !player.tracks.empty()) {
        auto path=player.tracks[player.index];
        unsigned id=player.generation;
        lock.unlock();
        bool ok=vr_decode_track(path,id);
        lock.lock();
        player.wake.wait(lock,[&]{return player.stopping || id!=player.generation || !player.count;});
        if(player.stopping || id!=player.generation)continue;
        player.reset_timeline();
        if(ok)player.index=(player.index+1)%player.tracks.size();
        else {
            std::fprintf(stderr,"[Music] Skipping unreadable track %s\n",path.u8string().c_str());
            player.tracks.erase(player.tracks.begin()+player.index);
            if(player.index==player.tracks.size())player.index=0;
        }
    }
}
} // namespace

bool vr_buffer_audio(unsigned id,const QuestRadioSong &info,const int16_t *stereo,size_t frames) {
    return player.enqueue(id,info,stereo,frames*2);
}

bool vr_buffer_picture(unsigned id,int64_t at,std::shared_ptr<const VrVideoPixels> pixels) {
    std::lock_guard<std::mutex> lock(player.mutex);
    if(player.stopping || id!=player.generation)return false;
    // Keep the most recent due frame and the small number covered by queued PCM.
    while(player.pictures.size()>1 && player.pictures[1].at<=player.played)player.pictures.pop_front();
    player.pictures.push_back({at,pixels?++player.serial:0,std::move(pixels)});
    player.has_picture=true;
    return true;
}

uint64_t vr_music_video_frame(uint8_t *out) {
    if(!player.has_picture || !player.scanning_or_ready)return 0;
    Picture frame{};
    {
        std::lock_guard<std::mutex> lock(player.mutex);
        if(!player.screen_active || player.stopping || SDL_GetTicks()-player.heartbeat>=500)return 0;
        for(auto it=player.pictures.rbegin();it!=player.pictures.rend();++it) {
            if(it->at<=player.played){frame=*it;break;}
        }
    }
    if(frame.pixels && out)std::memcpy(out,frame.pixels->data(),VR_VIDEO_BYTES);
    return frame.serial;
}

void vr_music_stock_track(int track,uint32_t left,uint32_t right) {
    uint64_t voices=track>=0 && left?(uint64_t(right)<<32)|left:0;
    if(!voices && !history.stock_voices)return;
    std::lock_guard<std::mutex> lock(history.mutex);
    if(track!=history.stock_track || voices!=history.stock_voices)history.reset_locked();
    history.stock_track=track;history.stock_voices=voices;history.stock_tick=SDL_GetTicks();
}

void vr_music_stock_samples(uint32_t voice,const float *mono,unsigned frames) {
    uint64_t voices=history.stock_voices;
    if(!voice || (voice!=uint32_t(voices) && voice!=uint32_t(voices>>32)))return;
    std::unique_lock<std::mutex> lock(history.mutex,std::try_to_lock);
    uint32_t now=SDL_GetTicks();
    if(!lock || voices!=history.stock_voices || now-history.stock_tick>=500)return;
    unsigned ch=voice==uint32_t(voices)?0:1;
    history.channels[ch].append(mono,frames,1,now);
    if(!(voices>>32))history.channels[1].append(mono,frames,1,now);
}

float vr_music_spectrum(float *out) {
    static Spectrum spectrum;
    SpectrumPcm pcm;
    float seconds;
    {
        std::lock_guard<std::mutex> lock(history.mutex);
        uint32_t now=SDL_GetTicks();
        if(!history.channels[0].ready(now) && !history.channels[1].ready(now)) {
            spectrum.levels.fill(0);spectrum.seen=0;spectrum.beat_average=spectrum.pulse=0;
            std::fill_n(out,QUEST_RADIO_BANDS,0.f);return 0.f;
        }
        if(spectrum.epoch!=history.epoch) {
            spectrum.levels.fill(0);spectrum.seen=0;spectrum.beat_average=spectrum.pulse=0;spectrum.epoch=history.epoch;
        }
        uint64_t captured=history.channels[0].frames+history.channels[1].frames;
        uint64_t elapsed=captured-spectrum.seen;
        // Up to 32 spectrum updates per second, independent of headset refresh.
        if(spectrum.seen && elapsed<2*1500) {
            std::copy(spectrum.levels.begin(),spectrum.levels.end(),out);return spectrum.pulse;
        }
        for(int ch=0;ch<2;++ch)history.channels[ch].snapshot(pcm[ch],now);
        seconds=float(elapsed)/(2.f*vr_media_rate);
        spectrum.seen=captured;
    }
    spectrum.measure(pcm,seconds);
    std::copy(spectrum.levels.begin(),spectrum.levels.end(),out);
    return spectrum.pulse;
}

void vr_music_start(const char *parent) {
    if(!parent || !*parent || player.worker.joinable())return;
    try {
        fs::path folder=fs::u8path(parent)/"Music";
        player.stopping=false;player.scanning_or_ready=true;
        player.worker=std::thread([folder] {
            try {run(folder);}
            catch(const std::exception &e) {std::fprintf(stderr,"[Music] %s\n",e.what());}
            player.scanning_or_ready=false;player.playing=false;
        });
        std::atexit(vr_music_stop);
    } catch(const std::exception &e) {
        player.scanning_or_ready=false;
        std::fprintf(stderr,"[Music] %s\n",e.what());
    }
}

void vr_music_stop(void) {
    vr_music_stock_track(-1,0,0);
    if(!player.worker.joinable())return;
    {
        std::lock_guard<std::mutex> lock(player.mutex);
        player.stopping=true;player.playing=false;
    }
    player.wake.notify_one();
    player.worker.join();
    std::lock_guard<std::mutex> lock(player.mutex);
    player.reset_timeline();player.screen_active=false;
    player.tracks.clear();player.ready=player.paused=false;
    player.index=player.head=player.count=0;
}

void vr_music_set_source(int source) {
    std::lock_guard<std::mutex> lock(player.mutex);
    player.source=source;
}

VrRadioPlayback vr_music_update(int driving,int can_mix,float volume,QuestRadioSong *song,int *paused) {
    if(!player.scanning_or_ready)return VR_RADIO_ORIGINAL;
    std::lock_guard<std::mutex> lock(player.mutex);
    bool use_custom=player.source!=QUEST_MUSIC_EA_ONLY && player.ready && !player.stopping && !player.tracks.empty();
    bool active=driving && use_custom;
    player.screen_active=active;
    bool playing=active && can_mix && !player.paused;
    if(player.playing && !playing)history.clear_custom();
    player.playing=playing;
    player.volume=std::clamp(volume,0.f,1.f);
    player.heartbeat=SDL_GetTicks();
    if(active){*song=player.song;*paused=player.paused;return VR_RADIO_CUSTOM;}
    return use_custom && player.source==QUEST_MUSIC_CUSTOM_ONLY?VR_RADIO_QUIET:VR_RADIO_ORIGINAL;
}

void vr_music_command(int action) {
    std::lock_guard<std::mutex> lock(player.mutex);
    size_t n=player.tracks.size();
    if(!n)return;
    if(action==QUEST_RADIO_PLAY)player.paused=!player.paused;
    if(action==QUEST_RADIO_PREV || action==QUEST_RADIO_NEXT) {
        player.index=(player.index+(action==QUEST_RADIO_NEXT?1:n-1))%n;
        ++player.generation;player.reset_timeline();
        player.count=0;player.paused=false;player.wake.notify_one();
    }
}

int vr_music_mix(const int16_t *game,int16_t *out,int samples) {
    if(samples<=0 || !player.playing)return 0;
    std::unique_lock<std::mutex> lock(player.mutex,std::try_to_lock);
    if(!lock || !player.playing)return 0;
    if(SDL_GetTicks()-player.heartbeat>=500)return 0;
    size_t n=std::min<size_t>(samples,player.count);
    if(!n)return 0;
    size_t first=std::min(n,player.ring.size()-player.head);
    history.capture_custom(player.ring.data()+player.head,first,player.ring.data(),n-first);
    for(size_t i=0;i<n;++i) {
        int mixed=game[i]+static_cast<int>(player.ring[player.head]*player.volume);
        out[i]=static_cast<int16_t>(std::clamp(mixed,-32768,32767));
        player.head=(player.head+1)%player.ring.size();
    }
    if(out!=game)std::copy(game+n,game+samples,out+n);
    player.count-=n;player.played+=static_cast<int64_t>(n/2);
    lock.unlock();player.wake.notify_one();
    return 1;
}
