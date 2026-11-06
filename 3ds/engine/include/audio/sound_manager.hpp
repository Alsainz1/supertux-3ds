// Native SDL_mixer audio backend for the Nintendo 3DS.
#pragma once
#include <memory>
#include <string>
#include "math/vector.hpp"
#include "util/currenton.hpp"
class SoundSource;
class StreamSoundSource;
class SoundManager final : public Currenton<SoundManager> {
  std::string m_current_music;
public:
  SoundManager();
  ~SoundManager() override;
  void enable_sound(bool);
  std::unique_ptr<SoundSource> create_sound_source(const std::string&);
  void play(const std::string&, const Vector& = Vector(-1, -1), float = .5f);
  void play(const std::string& name, float gain) { play(name, Vector(-1,-1),gain); }
  void manage_source(std::unique_ptr<SoundSource>);
  void preload(const std::string&);
  void set_listener_position(const Vector&);
  void set_listener_velocity(const Vector&) {}
  void set_listener_orientation(const Vector&, const Vector&) {}
  void enable_music(bool);
  void play_music(const std::string&, float);
  void play_music(const std::string& name, bool fade = false) { play_music(name,fade ? .5f : 0.f); }
  void pause_music(float = 0);
  void resume_music(float = 0);
  void stop_music(float = 0);
  void set_music_volume(int);
  void pause_sounds();
  void resume_sounds();
  void stop_sounds();
  void set_sound_volume(int);
  bool is_music_enabled() const;
  bool is_sound_enabled() const;
  bool is_audio_enabled() const;
  const std::string& get_current_music() const { return m_current_music; }
  void update();
  void register_for_update(StreamSoundSource*) {}
  void remove_from_update(StreamSoundSource*) {}
};
