// SDL_mixer -> SDL's native NDSP device. WAV effects and streamed Ogg music.
#include "audio/sound_manager.hpp"
#include "audio/sound_source.hpp"
#include "audio/dummy_sound_source.hpp"
#include "audio/sound_file.hpp"
#include "physfs/physfs_sdl.hpp"
#include "util/reader_document.hpp"
#include "util/reader_mapping.hpp"
#include "util/file_system.hpp"
#include "platform.hpp"
#include "packed_assets.hpp"
#include "performance.hpp"
#include "loading.hpp"
#include <SDL.h>
#include <SDL_mixer.h>
#include <physfs.h>
#include <zlib.h>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>
#include <deque>
#include <stdexcept>

namespace {
using Chunk = std::shared_ptr<Mix_Chunk>;
PackedAssets audio_pack("3ds-audio");
struct EffectRequest { Chunk chunk; int volume; Uint8 left, right; unsigned epoch; };
struct MusicEntry {
  std::vector<unsigned char> memory;
  Mix_Music* decoder=nullptr;
  float loop_begin=0,loop_at=-1;
  ~MusicEntry(){if(decoder)Mix_FreeMusic(decoder);}
};
const std::string star_music="music/misc/invincible.music";
struct AudioState {
  bool ready=false, sound=true, music=true;
  bool load_resume_music=false;unsigned load_resume_channels=0;int load_master_volume=MIX_MAX_VOLUME;
  int sound_volume=100, music_volume=50;
  Vector listener{0,0};
  std::map<std::string,Chunk> chunks;
  std::vector<std::unique_ptr<SoundSource>> managed;
  unsigned generations[32]{};
  Mix_Music* stream=nullptr;
  std::map<std::string,std::unique_ptr<MusicEntry>> music_cache;
  float loop_begin=0, loop_at=-1;
  std::atomic<unsigned> callbacks{0}, peak{0};
  Uint32 report_time=0, probe_time=0;
  bool probe=false;
  int probe_phase=0;
  SDL_mutex* effect_mutex=nullptr;
  SDL_sem* effect_signal=nullptr;
  SDL_Thread* effect_thread=nullptr;
  std::deque<EffectRequest> effects;
  std::atomic<bool> effect_running{false}, effects_enabled{true};
  std::atomic<unsigned> effect_epoch{0};
} state;

MusicEntry& prepare_music(const std::string& file){
  const auto key=!file.empty() && file.front()=='/'?file.substr(1):file;
  if(auto found=state.music_cache.find(key);found!=state.music_cache.end())return *found->second;
  auto entry=std::make_unique<MusicEntry>();std::string raw=file;
  if(file.size()>=6 && file.substr(file.size()-6)==".music"){
    auto doc=ReaderDocument::from_file(file);auto mapping=doc.get_root().get_mapping();
    std::string name;mapping.get("file",name);mapping.get("loop-begin",entry->loop_begin);mapping.get("loop-at",entry->loop_at);
    raw=FileSystem::normalize(FileSystem::dirname(file)+name);
  }
  const bool star=key==star_music;
  const bool packed_star=star && PHYSFS_exists("music/misc/invincible-3ds.pcmz");
  if(packed_star)raw="music/misc/invincible-3ds.pcmz";
  else if(star && PHYSFS_exists("music/misc/invincible-3ds.wav"))raw="music/misc/invincible-3ds.wav";
  SDL_RWops* input=get_physfs_SDLRWops(raw);
  // Keep compressed music in RAM: the real-time decoder must not wait behind
  // texture/save reads on the SD service. Only current/previous + star survive.
  {
    const auto bytes=SDL_RWsize(input);
    const auto limit=star?4*1024*1024:8*1024*1024;
    if(bytes>0 && bytes<=limit){
      entry->memory.resize(static_cast<size_t>(bytes));
      const bool complete=SDL_RWread(input,entry->memory.data(),1,entry->memory.size())==entry->memory.size();
      SDL_RWclose(input);input=nullptr;
      if(!complete)throw std::runtime_error("Short read preparing music");
      if(packed_star){
        if(entry->memory.size()<8 || std::memcmp(entry->memory.data(),"MW01",4))throw std::runtime_error("Invalid compressed star music");
        uint32_t size=0;std::memcpy(&size,entry->memory.data()+4,4);
        if(!size || size>4*1024*1024)throw std::runtime_error("Invalid star PCM size");
        std::vector<unsigned char> decoded(size);uLongf length=size;
        if(uncompress(decoded.data(),&length,entry->memory.data()+8,entry->memory.size()-8)!=Z_OK || length!=size)throw std::runtime_error("Star PCM decompression failed");
        entry->memory.swap(decoded);
      }
      input=SDL_RWFromConstMem(entry->memory.data(),static_cast<int>(entry->memory.size()));
    }
  }
  entry->decoder=Mix_LoadMUS_RW(input,1);
  if(!entry->decoder)throw std::runtime_error(Mix_GetError());
  ctr::log("[Audio cache] Prepared %s (%lu KiB resident)\n",raw.c_str(),static_cast<unsigned long>(entry->memory.size()/1024));
  auto* result=entry.get();state.music_cache.emplace(key,std::move(entry));return *result;
}
int effect_worker(void*) {
  while (state.effect_running.load()) {
    if (SDL_SemWait(state.effect_signal)<0) break;
    if (!state.effect_running.load()) break;
    SDL_LockMutex(state.effect_mutex);
    if (state.effects.empty()) { SDL_UnlockMutex(state.effect_mutex);continue; }
    auto request=std::move(state.effects.front());state.effects.pop_front();
    SDL_UnlockMutex(state.effect_mutex);
    if (!state.effects_enabled.load() || request.epoch!=state.effect_epoch.load()) continue;
    while(ctr_loading::active() && state.effect_running.load())SDL_Delay(5);
    if(!state.effect_running.load())break;
    const int channel=Mix_PlayChannel(-1,request.chunk.get(),0);
    if (channel<0) continue;
    Mix_Volume(channel,request.volume);
    Mix_SetPanning(channel,request.left,request.right);
    if (!state.effects_enabled.load() || request.epoch!=state.effect_epoch.load()) Mix_HaltChannel(channel);
  }
  return 0;
}

void discard_effect_requests() {
  ++state.effect_epoch;
  if (!state.effect_mutex) return;
  SDL_LockMutex(state.effect_mutex);state.effects.clear();SDL_UnlockMutex(state.effect_mutex);
}
void meter(void*, Uint8* stream, int bytes) {
  const auto* samples=reinterpret_cast<const Sint16*>(stream);
  unsigned peak=0;
  for(int i=0;i<bytes/2;++i) peak=std::max(peak,unsigned(std::abs(int(samples[i]))));
  unsigned previous=state.peak.load(std::memory_order_relaxed);
  while(previous<peak && !state.peak.compare_exchange_weak(previous,peak,std::memory_order_relaxed)) {}
  state.callbacks.fetch_add(1,std::memory_order_relaxed);
}
Chunk load_chunk(const std::string& file) {
  const auto key=!file.empty() && file.front()=='/' ? file.substr(1) : file;
  if(const auto i=state.chunks.find(key);i!=state.chunks.end())return i->second;
  auto pcm=std::make_shared<std::vector<unsigned char>>();
  if(audio_pack.read(file,*pcm) && !pcm->empty() && pcm->size()%4==0) {
    auto* chunk=Mix_QuickLoad_RAW(pcm->data(),static_cast<Uint32>(pcm->size()));
    if(chunk) {
      Chunk result(chunk,[pcm](Mix_Chunk* p){Mix_FreeChunk(p);});
      state.chunks[key]=result;return result;
    }
  }
  auto* chunk=Mix_LoadWAV_RW(get_physfs_SDLRWops(file),1);
  if(!chunk)throw std::runtime_error(Mix_GetError());
  Chunk result(chunk,Mix_FreeChunk); state.chunks[key]=result; return result;
}
class MixerSource final : public SoundSource {
  Chunk chunk;
  int channel=-1;
  unsigned generation=0;
  bool looping=false, relative=true;
  float gain=1, reference=128;
  Vector position{0,0};
  int last_volume=-1,last_left=-1,last_right=-1;
  unsigned position_updates=0,mixer_writes=0;
  std::string filename;
  bool owns() const { return state.ready && channel>=0 && state.generations[channel]==generation; }
  void apply() {
    if(!owns())return;
    const float distance=relative ? 0 : std::abs(position.x-state.listener.x);
    const float attenuation=distance<=reference ? 1 : reference/std::max(reference,distance);
    const int volume=std::clamp(int(gain*attenuation*state.sound_volume*.01f*MIX_MAX_VOLUME),0,MIX_MAX_VOLUME);
    if(volume!=last_volume){Mix_Volume(channel,volume);last_volume=volume;++mixer_writes;}
    const float pan=relative ? 0 : std::clamp((position.x-state.listener.x)/800.f,-1.f,1.f);
    const int left=int(255*std::min(1.f,1-pan)),right=int(255*std::min(1.f,1+pan));
    if(left!=last_left || right!=last_right){Mix_SetPanning(channel,Uint8(left),Uint8(right));last_left=left;last_right=right;++mixer_writes;}
  }
public:
  explicit MixerSource(Chunk data,const std::string& name):chunk(std::move(data)),filename(name){}
  ~MixerSource() override { if(ctr_perf::enabled() && position_updates>10)ctr::log("[Audio cache] %s: %u position updates, %u mixer writes\n",filename.c_str(),position_updates,mixer_writes);stop(); }
  void play() override {
    if(!state.ready || !state.sound)return;
    stop();
    // The worker's one-shots use the remaining channels. Reserve eight for
    // managed/looping sources so channel ownership never races between threads.
    int available=-1;
    for (int i=0;i<8;++i) if (!Mix_Playing(i)) {available=i;break;}
    channel=available<0 ? -1 : Mix_PlayChannel(available,chunk.get(),looping ? -1 : 0);
    if(channel>=0){generation=++state.generations[channel];last_volume=last_left=last_right=-1;apply();if(ctr_loading::active()){state.load_resume_channels|=1u<<channel;Mix_Pause(channel);}}
  }
  void stop(bool = true) override { if(owns())Mix_HaltChannel(channel);channel=-1; }
  void pause() override { if(owns())Mix_Pause(channel); }
  bool playing() const override { return owns() && Mix_Playing(channel)!=0; }
  void set_looping(bool value) override { looping=value; }
  void set_relative(bool value) override { relative=value;apply(); }
  void set_gain(float value) override { gain=std::max(0.f,value);apply(); }
  void set_pitch(float) override {} // Doppler/pitch modulation is not implemented by this backend.
  void set_position(const Vector& value) override {++position_updates;position=value;apply();}
  void set_velocity(const Vector&) override {}
  void set_reference_distance(float value) override { reference=std::max(1.f,value);apply(); }
};
}

SoundManager::SoundManager() {
  if(SDL_InitSubSystem(SDL_INIT_AUDIO)<0){ctr::log("[Audio] SDL init failed: %s\n",SDL_GetError());return;}
  const int formats=Mix_Init(MIX_INIT_OGG);
  if(!(formats&MIX_INIT_OGG)){ctr::log("[Audio] Ogg decoder failed: %s\n",Mix_GetError());return;}
  if(Mix_OpenAudio(32000,AUDIO_S16SYS,2,1024)<0){
    ctr::log("[Audio] NDSP device failed: %s\n",Mix_GetError());return;
  }
  Mix_AllocateChannels(32);
  Mix_ReserveChannels(8);
  state.ready=true; state.report_time=SDL_GetTicks();
  // Short effects fit in a bounded cache. Decode on the PC, read once here,
  // so a first jump, coin, growth or 1-up need not stop the game to decode Ogg.
  size_t warmed=0; unsigned count=0;
  constexpr size_t budget=8*1024*1024;
  const auto& entries=audio_pack.entries();
  auto warm=[&](const std::string& name) {
    const auto found=entries.find('/'+name);
    if(found==entries.end() || found->second.raw>budget-warmed || state.chunks.count(name))return;
    try { load_chunk(name);warmed+=found->second.raw;++count; } catch(...) {}
  };
  for(const auto* name:{"invincible_start.ogg","explosion.wav","firecracker.ogg","fizz.wav","welldone.ogg","tada.ogg","coins_cleared.ogg","grow.ogg","grow.wav","upgrade.wav","lifeup.wav","bigjump.wav","jump.wav","coin.wav","brick.wav","shoot.wav"})
    warm(std::string("sounds/")+name);
  for(const auto& entry:entries) if(entry.first.rfind("/sounds/",0)==0)warm(entry.first.substr(1));
  ctr::log("[Audio] Warmed %u effects, %lu KiB\n",count,static_cast<unsigned long>(warmed/1024));
  state.effect_mutex=SDL_CreateMutex();state.effect_signal=SDL_CreateSemaphore(0);
  if (state.effect_mutex && state.effect_signal) {
    state.effect_running=true;
    state.effect_thread=SDL_CreateThread(effect_worker,"effects",nullptr);
    if (!state.effect_thread) state.effect_running=false;
  }
  ctr::log("[Audio] One-shot worker: %s\n",state.effect_thread ? "ready" : "synchronous fallback");
  if(auto* flag=std::fopen("sdmc:/3ds/supertux070/audio-probe.flag","r")){state.probe=true;std::fclose(flag);}
  if(state.probe || ctr_perf::enabled())Mix_SetPostMix(meter,nullptr);
  ctr::log("[Audio] NDSP ready: 32000 Hz stereo\n[Audio] WAV effects + Ogg streaming\n");
}
SoundManager::~SoundManager(){
  state.effect_running=false;
  if (state.effect_thread) {
    SDL_SemPost(state.effect_signal);SDL_WaitThread(state.effect_thread,nullptr);state.effect_thread=nullptr;
  }
  state.effects.clear();
  if(state.effect_signal){SDL_DestroySemaphore(state.effect_signal);state.effect_signal=nullptr;}
  if(state.effect_mutex){SDL_DestroyMutex(state.effect_mutex);state.effect_mutex=nullptr;}
  if(state.ready){Mix_SetPostMix(nullptr,nullptr);Mix_HaltChannel(-1);Mix_HaltMusic();}
  state.managed.clear();state.chunks.clear();
  audio_pack.close();
  state.stream=nullptr;state.music_cache.clear();
  if(state.ready)Mix_CloseAudio();
  state.ready=false;Mix_Quit();SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
void ctr_loading::begin_audio(){
  if(!state.ready)return;
  state.load_resume_music=Mix_PlayingMusic()&&!Mix_PausedMusic();state.load_resume_channels=0;
  for(int i=0;i<32;++i)if(Mix_Playing(i)&&!Mix_Paused(i))state.load_resume_channels|=1u<<i;
  state.load_master_volume=Mix_MasterVolume(0);Mix_VolumeMusic(0);
  Mix_PauseMusic();Mix_Pause(-1);
}
void ctr_loading::end_audio(){
  if(!state.ready)return;
  Mix_VolumeMusic(state.music_volume*MIX_MAX_VOLUME/100);Mix_MasterVolume(state.load_master_volume);
  if(state.load_resume_music&&state.music)Mix_ResumeMusic();
  for(int i=0;i<32;++i)if(state.load_resume_channels&(1u<<i))Mix_Resume(i);
  state.load_resume_channels=0;state.load_resume_music=false;
}

bool SoundManager::is_audio_enabled() const{return state.ready;}
bool SoundManager::is_music_enabled() const{return state.ready&&state.music;}
bool SoundManager::is_sound_enabled() const{return state.ready&&state.sound;}
void SoundManager::enable_sound(bool enabled){state.sound=enabled;state.effects_enabled=enabled;if(!enabled&&state.ready){discard_effect_requests();Mix_HaltChannel(-1);}}
void SoundManager::enable_music(bool enabled){state.music=enabled;if(!enabled&&state.ready)Mix_PauseMusic();else if(state.ready)Mix_ResumeMusic();}
void SoundManager::set_listener_position(const Vector& position){state.listener=position;}
void SoundManager::set_music_volume(int volume){state.music_volume=std::clamp(volume,0,100);if(state.ready)Mix_VolumeMusic(ctr_loading::active()?0:state.music_volume*MIX_MAX_VOLUME/100);}
void SoundManager::set_sound_volume(int volume){state.sound_volume=std::clamp(volume,0,100);}
std::unique_ptr<SoundSource> SoundManager::create_sound_source(const std::string& file){
  if(!state.ready)return create_dummy_sound_source();
  try{return std::make_unique<MixerSource>(load_chunk(file),file);}
  catch(const std::exception& e){ctr::log("[Audio] Effect %s: %s\n",file.c_str(),e.what());return create_dummy_sound_source();}
}
void SoundManager::preload(const std::string& file){if(state.ready)try{load_chunk(file);}catch(const std::exception& e){ctr::log("[Audio] Preload: %s\n",e.what());}}
void SoundManager::manage_source(std::unique_ptr<SoundSource> source){state.managed.push_back(std::move(source));}
void SoundManager::play(const std::string& file,const Vector& position,float gain){
  if(!is_sound_enabled())return;
  if(state.effect_thread) {
    try {
      const bool relative=position.x<0 && position.y<0;
      const float distance=relative ? 0 : std::abs(position.x-state.listener.x);
      const float attenuation=distance<=128 ? 1 : 128.f/std::max(128.f,distance);
      const float pan=relative ? 0 : std::clamp((position.x-state.listener.x)/800.f,-1.f,1.f);
      EffectRequest request{load_chunk(file),
        std::clamp(int(std::max(0.f,gain)*attenuation*state.sound_volume*.01f*MIX_MAX_VOLUME),0,MIX_MAX_VOLUME),
        Uint8(255*std::min(1.f,1-pan)),Uint8(255*std::min(1.f,1+pan)),state.effect_epoch.load()};
      SDL_LockMutex(state.effect_mutex);
      const bool queued=state.effects.size()<64;
      if(queued)state.effects.push_back(std::move(request));
      SDL_UnlockMutex(state.effect_mutex);
      if(queued)SDL_SemPost(state.effect_signal);
    }catch(const std::exception& e){ctr::log("[Audio] Effect %s: %s\n",file.c_str(),e.what());}
    return;
  }
  auto source=create_sound_source(file);source->set_position(position);source->set_relative(position.x<0&&position.y<0);
  source->set_gain(gain);source->play();manage_source(std::move(source));
}
void SoundManager::play_music(const std::string& file,float fade){
  if(!state.ready||file.empty()){stop_music(fade);return;}
  if(m_current_music==file && Mix_PlayingMusic())return;
  try{
    // Prepare before play, retain the level decoder across a star, and keep the
    // short native-rate star track in RAM so it needs no SD reads or Ogg decode.
    const auto key=file.front()=='/'?file.substr(1):file;
    if(key!=star_music)try{prepare_music(star_music);}catch(const std::exception& e){ctr::log("[Audio] Star warmup: %s\n",e.what());}
    auto& entry=prepare_music(file);
    auto* next=entry.decoder;
    Mix_HaltMusic();state.stream=next;state.loop_begin=entry.loop_begin;state.loop_at=entry.loop_at;
    if(key!=star_music){
      for(auto it=state.music_cache.begin();it!=state.music_cache.end();)
        if(it->first!=key && it->first!=star_music)it=state.music_cache.erase(it);else ++it;
    }
    m_current_music=file;
    set_music_volume(state.music_volume);
    ctr::log("[Audio] Starting stream\n");
    if(Mix_FadeInMusic(next,-1,std::max(0,int(fade*1000)))<0)throw std::runtime_error(Mix_GetError());
    if(ctr_loading::active()){state.load_resume_music=state.music;Mix_PauseMusic();}
    else if(!state.music)Mix_PauseMusic();
    ctr::log("[Audio] Music: %s\n",file.c_str());
  }catch(const std::exception& e){ctr::log("[Audio] Music failed: %s\n",e.what());}
}
void SoundManager::pause_music(float){if(state.ready)Mix_PauseMusic();}
void SoundManager::resume_music(float){if(state.ready&&state.music){if(ctr_loading::active())state.load_resume_music=true;else Mix_ResumeMusic();}}
void SoundManager::stop_music(float fade){if(state.ready){if(fade>0)Mix_FadeOutMusic(int(fade*1000));else Mix_HaltMusic();}m_current_music.clear();}
void SoundManager::pause_sounds(){if(state.ready)Mix_Pause(-1);}
void SoundManager::resume_sounds(){if(state.ready)Mix_Resume(-1);}
void SoundManager::stop_sounds(){discard_effect_requests();if(state.ready)Mix_HaltChannel(-1);}
void SoundManager::update(){
  if(!state.ready)return;
  state.managed.erase(std::remove_if(state.managed.begin(),state.managed.end(),[](const auto& s){return !s->playing();}),state.managed.end());
  if(state.stream&&state.loop_at>state.loop_begin&&Mix_PlayingMusic()&&!Mix_PausedMusic()&&
     Mix_GetMusicPosition(state.stream)>=state.loop_at)Mix_SetMusicPosition(state.loop_begin);
  const Uint32 now=SDL_GetTicks();
  // Optional development probe; the marker is never part of the release package.
  if(state.probe && state.stream && Mix_PlayingMusic()) {
    if(state.probe_phase==0){state.probe_time=now;state.probe_phase=1;}
    else if(state.probe_phase==1 && now-state.probe_time>=2000){
      ctr::log("[Audio test] Music PCM peak %u\n",state.peak.load());
      Mix_PauseMusic();state.peak=0;
      play("sounds/jump.wav",1.f);
      state.probe_phase=2;state.probe_time=now;
    } else if(state.probe_phase==2 && now-state.probe_time>=750){
      ctr::log("[Audio test] Effect PCM peak %u\n",state.peak.load());
      Mix_ResumeMusic();state.probe=false;
      std::remove("sdmc:/3ds/supertux070/audio-probe.flag");
    }
  }
  if(ctr_perf::enabled() && now-state.report_time>=5000){
    ctr::log("[Audio] callbacks %u, PCM peak %u\n",state.callbacks.exchange(0),state.peak.exchange(0));state.report_time=now;
  }
}
std::unique_ptr<SoundFile> load_sound_file(const std::string&){throw std::runtime_error("Audio metadata editor is unavailable on 3DS");}
