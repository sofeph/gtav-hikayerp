#pragma once

#include <string>
#include <vector>

// Plays the phone's .ogg sounds from data/phone_sfx/<category>/ through XAudio2.
// Decoding happens on a worker thread, so calls are cheap and safe from the game script thread.
namespace hikaye::phone_audio {

enum class Channel { Ringtone, Notification, Alarm, Ui, Preview, Count };

// Sound names (file stems, UTF-8) in a category: "ringtones", "notifications", "alarms", "ui". Sorted.
std::vector<std::string> List(const std::string& category);

// Human-readable label: "S_Good_News" -> "Good News".
std::string Label(const std::string& name);

// Replaces whatever the channel was playing. volume: 0..1.
void Play(Channel channel, const std::string& category, const std::string& name, bool loop, float volume);
void Stop(Channel channel);
bool IsPlaying(Channel channel);

void Shutdown();

}  // namespace hikaye::phone_audio
