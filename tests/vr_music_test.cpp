#include "vr_music.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>

namespace fs=std::filesystem;
using namespace std::chrono_literals;
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"Line %d: %s\n",__LINE__,#x);return 1;} } while(0)

/* Short mono fixtures also exercise conversion to the APU's 48 kHz stereo. */
static void wav(const fs::path &path,int value,float hz=0)
{
    std::ofstream f(path,std::ios::binary);
    auto le=[&](unsigned n,int bytes){for(int i=0;i<bytes;++i)f.put(static_cast<char>(n>>(8*i)));};
    f.write("RIFF",4);le(36+24000,4);f.write("WAVEfmt ",8);le(16,4);
    le(1,2);le(1,2);le(24000,4);le(48000,4);le(2,2);le(16,2);
    f.write("data",4);le(24000,4);
    for(int i=0;i<12000;++i)le(hz?static_cast<int>(value*std::sin(6.283185307179586*hz*i/24000.)):value,2);
}

static bool wait_for_track(QuestRadioSong &song,const std::string &previous)
{
    for(int i=0;i<1000;++i) {
        int paused=0;
        if(vr_music_update(1,1,.5f,&song,&paused) && song.title[0] && song.title!=previous)return true;
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

static int stock_spectrum()
{
    std::array<float,QUEST_RADIO_BANDS> bars{};
    std::array<float,4096> left{},right{};
    auto tone=[&](float hz) {
        for(size_t i=0;i<left.size();++i) {
            left[i]=float(12000*std::sin(6.283185307179586*hz*i/48000.));
            right[i]=-left[i];
        }
    };
    auto feed=[&] {
        vr_music_stock_samples(17,left.data(),(unsigned)left.size());
        vr_music_stock_samples(23,right.data(),(unsigned)right.size());
    };
    vr_music_stock_track(3,17,23);
    tone(93.75f);
    feed();
    CHECK(vr_music_spectrum(bars.data())>.9f);
    CHECK(bars[1]>.75f && bars[13]<.05f);
    // Held bass settles, a quiet interval releases the pulse, and a fresh
    // attack lights both eyes from the same cached audio window.
    for(int block=0;block<18;++block) {
        feed();
        vr_music_spectrum(bars.data());
    }
    CHECK(vr_music_spectrum(bars.data())<.05f);
    left.fill(0);right.fill(0);
    for(int block=0;block<8;++block) {
        feed();
        vr_music_spectrum(bars.data());
    }
    tone(93.75f);
    feed();
    float next_beat=vr_music_spectrum(bars.data());
    CHECK(next_beat>.9f && next_beat==vr_music_spectrum(bars.data()));
    vr_music_stock_track(5,17,23);
    vr_music_spectrum(bars.data());
    CHECK(*std::max_element(bars.begin(),bars.end())==0);
    tone(6000.f);
    // Ordinary submissions straddle both the FFT window and the ring wrap.
    left.fill(0);
    for(size_t i=0;i+192<=right.size();i+=192) {
        vr_music_stock_samples(17,left.data()+i,192);
        vr_music_stock_samples(23,right.data()+i,192);
    }
    vr_music_spectrum(bars.data());
    CHECK(bars[13]>.75f && bars[1]<.05f);
    right.fill(0);
    for(int i=0;i<8;++i)feed();
    vr_music_spectrum(bars.data());
    CHECK(*std::max_element(bars.begin(),bars.end())==0);
    vr_music_stock_track(-1,0,0);
    tone(6000.f);
    feed();
    vr_music_spectrum(bars.data());
    CHECK(*std::max_element(bars.begin(),bars.end())==0);
    std::puts("Stock radio spectrum: bass, treble, opposite-phase stereo, silence and inactive radio passed.");
    return 0;
}

static int custom_visual_gain(const fs::path &parent)
{
    auto path=parent/"Music"/"tone.wav";
    wav(path,12000,750.f);
    std::array<float,QUEST_RADIO_BANDS> reference{},bars{};
    std::array<int16_t,2048> game{},mix{},full{};
    float reference_pulse=0;
    for(float volume:{1.f,.1f,0.f}) {
        QuestRadioSong song{};int paused=0;
        vr_music_start(parent.u8string().c_str());
        CHECK(wait_for_track(song,"") && vr_music_update(1,1,volume,&song,&paused)==VR_RADIO_CUSTOM);
        int tries=0;
        while(!vr_music_mix(game.data(),mix.data(),(int)mix.size()) && ++tries<1000)
            std::this_thread::sleep_for(1ms);
        CHECK(tries<1000);
        float pulse=vr_music_spectrum(bars.data());
        if(volume==1.f) {
            full=mix;reference=bars;reference_pulse=pulse;
            CHECK(*std::max_element(full.begin(),full.end())>6000);
            CHECK(*std::max_element(bars.begin(),bars.end())>.75f && pulse>.1f);
        } else {
            for(size_t i=0;i<mix.size();++i)CHECK(mix[i]==static_cast<int16_t>(full[i]*volume));
            CHECK(bars==reference && pulse==reference_pulse);
        }
        CHECK(vr_music_update(1,1,0,&song,&paused)==VR_RADIO_CUSTOM);
        CHECK(vr_music_spectrum(bars.data())==pulse && bars==reference);
        vr_music_stop();
    }
    fs::remove(path);
    std::puts("Identical spectrum and LED pulse at full, 10% and muted output.");
    return 0;
}

static int exercise(const fs::path &parent)
{
    QuestRadioSong song{};
    int paused=0;
    vr_music_start(parent.u8string().c_str());
    CHECK(wait_for_track(song,""));
    std::string first=song.title;
    std::array<int16_t,512> game{},mix{};
    game.fill(500);
    CHECK(vr_music_mix(game.data(),mix.data(),512));
    CHECK(std::any_of(mix.begin(),mix.end(),[](int16_t x){return x!=500;}));
    vr_music_set_source(QUEST_MUSIC_CUSTOM_ONLY);
    CHECK(vr_music_update(0,1,.5f,&song,&paused)==VR_RADIO_QUIET);
    CHECK(!vr_music_mix(game.data(),mix.data(),512));
    CHECK(vr_music_update(1,1,.5f,&song,&paused)==VR_RADIO_CUSTOM);
    vr_music_set_source(QUEST_MUSIC_EA_ONLY);
    CHECK(vr_music_update(1,1,.5f,&song,&paused)==VR_RADIO_ORIGINAL);
    CHECK(!vr_music_mix(game.data(),mix.data(),512) && !vr_music_video_frame(nullptr));
    CHECK(vr_music_update(0,1,.5f,&song,&paused)==VR_RADIO_ORIGINAL);
    vr_music_set_source(QUEST_MUSIC_AUTO);
    CHECK(vr_music_update(1,0,.5f,&song,&paused)); /* wait for the retail music voice to pause */
    CHECK(!vr_music_mix(game.data(),mix.data(),512));
    vr_music_command(QUEST_RADIO_PLAY);
    CHECK(vr_music_update(1,1,.5f,&song,&paused) && paused);
    CHECK(!vr_music_mix(game.data(),mix.data(),512));
    vr_music_command(QUEST_RADIO_PLAY);
    CHECK(!vr_music_update(0,1,.5f,&song,&paused) && !vr_music_mix(game.data(),mix.data(),512));
    CHECK(vr_music_update(1,1,0,&song,&paused) && !paused);
    CHECK(vr_music_mix(game.data(),mix.data(),512) && game==mix);
    vr_music_command(QUEST_RADIO_NEXT);
    CHECK(wait_for_track(song,first));
    std::string second=song.title;
    vr_music_command(QUEST_RADIO_PREV);
    CHECK(wait_for_track(song,second) && first==song.title);
    std::set<std::string> heard={first};
    std::string previous=first;
    for(int i=0;i<3000;++i) {
        CHECK(vr_music_update(1,1,.5f,&song,&paused));
        if(previous!=song.title) {
            if(!heard.insert(song.title).second)break;
            previous=song.title;
        }
        vr_music_mix(game.data(),mix.data(),512);
        std::this_thread::sleep_for(1ms);
    }
    CHECK(heard.size()==3 && song.title==first);
    vr_music_stop();
    CHECK(!vr_music_mix(game.data(),mix.data(),512));
    return 0;
}

/* tests/vr_radio_video_test.py supplies a two-color portrait clip (AAC), a
 * silent widescreen clip, an audio-only MP4 and an unreadable MP4. */
static int exercise_video(const fs::path &parent)
{
    QuestRadioSong song{};
    int paused=0;
    vr_music_start(parent.u8string().c_str());
    CHECK(wait_for_track(song,""));
    std::set<std::string> visited;
    std::array<uint8_t,VR_VIDEO_BYTES> pixels{};
    std::array<int16_t,512> game{},mix{};
    for(int track=0;track<3;++track) {
        std::string title=song.title;
        CHECK(visited.insert(title).second);
        if(title=="Music") {
            CHECK(!vr_music_video_frame(nullptr));
            std::array<float,QUEST_RADIO_BANDS> levels{};
            // Only the custom 1.5 kHz tone should drive the display, even
            // when the game is louder and has energy in a different band.
            for(size_t i=0;i<game.size();++i)
                game[i]=(i/2)%8<4?23000:-23000;
            CHECK(vr_music_update(1,1,1,&song,&paused));
            for(int i=0;i<12;++i) {
                vr_music_mix(game.data(),mix.data(),(int)mix.size());
                std::this_thread::sleep_for(1ms);
            }
            game.fill(0);
            vr_music_spectrum(levels.data());
            CHECK(levels[9]>.5f && levels[13]<.1f);
            vr_music_command(QUEST_RADIO_PLAY);
            CHECK(vr_music_update(1,1,1,&song,&paused) && paused);
            vr_music_spectrum(levels.data());
            CHECK(*std::max_element(levels.begin(),levels.end())==0);
            vr_music_command(QUEST_RADIO_NEXT);
        } else {
            bool audio=title=="Portrait";
            CHECK(audio || title=="Silent");
            CHECK(vr_music_update(1,1,1,&song,&paused));
            uint64_t first=vr_music_video_frame(pixels.data());
            CHECK(first);
            auto pixel=[&](int x,int channel){return pixels[4*(VR_VIDEO_WIDTH*(VR_VIDEO_HEIGHT/2)+x)+channel];};
            CHECK(pixel(VR_VIDEO_WIDTH/2,0)>220 && pixel(VR_VIDEO_WIDTH/2,2)<20);
            CHECK(audio?pixel(4,0)<10:pixel(4,0)>220);
            vr_music_command(QUEST_RADIO_PLAY);
            CHECK(vr_music_update(1,1,1,&song,&paused) && paused);
            std::this_thread::sleep_for(100ms);
            CHECK(!vr_music_mix(game.data(),mix.data(),512) && vr_music_video_frame(nullptr)==first);
            vr_music_command(QUEST_RADIO_PLAY);
            CHECK(!vr_music_update(0,1,1,&song,&paused) && !vr_music_video_frame(nullptr));
            CHECK(vr_music_update(1,1,1,&song,&paused) && !paused && vr_music_video_frame(nullptr)==first);
            vr_music_set_source(QUEST_MUSIC_EA_ONLY);
            CHECK(vr_music_update(1,1,1,&song,&paused)==VR_RADIO_ORIGINAL && !vr_music_video_frame(nullptr));
            vr_music_set_source(QUEST_MUSIC_CUSTOM_ONLY);
            CHECK(vr_music_update(0,1,1,&song,&paused)==VR_RADIO_QUIET && !vr_music_video_frame(nullptr));
            CHECK(vr_music_update(1,1,1,&song,&paused)==VR_RADIO_CUSTOM && vr_music_video_frame(nullptr)==first);
            vr_music_set_source(QUEST_MUSIC_AUTO);
            int samples=0;
            bool heard=false,saw_blue=false,saw_tail=false,video_pulsed=false;
            for(int i=0;i<2000;++i) {
                bool muted=samples<6000;
                CHECK(vr_music_update(1,1,muted?0.f:1.f,&song,&paused));
                if(title!=song.title)break;
                if(vr_music_mix(game.data(),mix.data(),512)) {
                    samples+=256;
                    if(muted)CHECK(mix==game);
                    if(std::any_of(mix.begin(),mix.end(),[](int16_t x){return x!=0;}))heard=true;
                }
                uint64_t frame=vr_music_video_frame(pixels.data());
                if(frame && muted) {
                    std::array<float,QUEST_RADIO_BANDS> bands{};
                    if(vr_music_spectrum(bands.data())>.1f)video_pulsed=true;
                }
                if(frame && pixel(VR_VIDEO_WIDTH/2,2)>220) {
                    CHECK(samples>=23500);
                    if(!saw_blue)CHECK(samples<26000);
                    saw_blue=true;
                }
                if(audio && samples>51000 && samples<64000 && !frame) {
                    std::array<float,QUEST_RADIO_BANDS> levels{};
                    vr_music_spectrum(levels.data());
                    if(levels[5]>.45f)saw_tail=true;
                }
                std::this_thread::sleep_for(1ms);
            }
            CHECK(saw_blue && heard==audio && video_pulsed==audio && saw_tail==audio && title!=song.title);
            CHECK(audio?(samples>=66500 && samples<70500):(samples>=47500 && samples<51000));
        }
        CHECK(wait_for_track(song,title));
    }
    CHECK(visited.count("Music") && visited.count("Silent") && visited.count("Portrait"));
    for(int i=0;i<2 && std::string(song.title)!="Music";++i) {
        std::string previous=song.title;
        vr_music_command(QUEST_RADIO_NEXT);
        CHECK(wait_for_track(song,previous));
    }
    CHECK(std::string(song.title)=="Music");
    vr_music_command(QUEST_RADIO_PREV);
    CHECK(wait_for_track(song,"Music") && vr_music_video_frame(nullptr));
    std::string previous=song.title;
    vr_music_command(QUEST_RADIO_PLAY);
    CHECK(vr_music_update(1,1,1,&song,&paused) && paused);
    vr_music_command(QUEST_RADIO_NEXT);
    CHECK(wait_for_track(song,previous) && std::string(song.title)=="Music");
    CHECK(!vr_music_video_frame(nullptr) && vr_music_update(1,1,1,&song,&paused) && !paused);
    vr_music_stop();
    CHECK(!vr_music_video_frame(nullptr));
    std::puts("Radio video: picture timing, aspect ratio, AAC/silent playback, pause, menu and playlist transitions passed.");
    return 0;
}

int main(int argc,char **argv)
{
    CHECK(stock_spectrum()==0);
    {
        int16_t silence[1920]={0},click[1920]={0};
        vr_music_shift_click();CHECK(vr_music_mix(silence,click,1920));
        int peak=0;for(int i=0;i<1920;++i)peak=std::max(peak,std::abs((int)click[i]));
        CHECK(peak>500 && peak<3000);
        CHECK(!vr_music_mix(silence,click,1920));
        std::puts("Shift sound: finite 20-ms click and return to silence passed.");
    }
    if(argc==3 && std::string(argv[1])=="--video")return exercise_video(fs::u8path(argv[2]));
    /* Optional folder of three short, distinct tracks for compressed-format QA. */
    if(argc==2)return exercise(fs::u8path(argv[1]));
    auto parent=fs::temp_directory_path()/("nfsu2-music-test-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(fs::create_directory(parent));
    auto music=parent/"Music";
    QuestRadioSong song{};int paused=0;
    vr_music_set_source(QUEST_MUSIC_CUSTOM_ONLY);
    vr_music_start(parent.u8string().c_str());
    CHECK(vr_music_update(0,1,1,&song,&paused)==VR_RADIO_ORIGINAL);
    CHECK(vr_music_update(1,1,1,&song,&paused)==VR_RADIO_ORIGINAL);
    vr_music_stop();
    vr_music_set_source(QUEST_MUSIC_AUTO);
    CHECK(fs::is_directory(music));
    CHECK(custom_visual_gain(parent)==0);
    const char *names[]={"one.wav","two.WAV","three.wav","broken.mp3"};
    for(int i=0;i<3;++i)wav(music/names[i],2000+i*2000);
    std::ofstream(music/names[3])<<"Not audio";
    int result=exercise(parent);
    vr_music_stop();
    for(int i=0;i<3;++i)fs::remove(music/names[i]);
    vr_music_start(parent.u8string().c_str());
    vr_music_set_source(QUEST_MUSIC_CUSTOM_ONLY);
    for(int i=0;i<30;++i) {
        CHECK(vr_music_update(1,1,1,&song,&paused)==VR_RADIO_ORIGINAL);
        CHECK(vr_music_update(0,1,1,&song,&paused)==VR_RADIO_ORIGINAL);
        std::this_thread::sleep_for(2ms);
    }
    vr_music_stop();
    fs::remove(music/names[3]);fs::remove(music);fs::remove(parent);
    if(!result)std::puts("Custom music: shuffle cycle, skip/back, pause/resume, gain and invalid-file fallback passed.");
    return result;
}
