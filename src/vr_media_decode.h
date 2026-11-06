#pragma once
#include "vr_music.h"
#include <array>
#include <filesystem>
#include <memory>

using VrVideoPixels=std::array<uint32_t,VR_VIDEO_WIDTH*VR_VIDEO_HEIGHT>;
constexpr int vr_media_rate=48000;
bool vr_decode_track(const std::filesystem::path &path,unsigned generation);
bool vr_buffer_audio(unsigned generation,const QuestRadioSong &info,const int16_t *stereo,size_t frames);
bool vr_buffer_picture(unsigned generation,int64_t at,std::shared_ptr<const VrVideoPixels> pixels);
