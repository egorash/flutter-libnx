// Аудио-движок поверх libnx audren. Схема и обоснование - в
// include/flutter_libnx/audio_horizon.h, здесь только реализация.
//
// Раскладка:
//
//   * Один поток (AudioLoop) единолично владеет драйвером и всеми
//     голосами. Обращения из канала Dart кладут��я в очередь команд,
//     голоса трогает только этот поток - блокировок на полях не нужно.
//   * Игрок = голос audren. Смешивает сам audren, поэтому программного
//     микшера здесь нет: при 31 одновременном источнике суммирование -
//     работа сервиса, а не наша.
//   * Данные голоса лежат в mempool-буфере audren (требование сервиса) в
//     четырёх слотах. Слот свободен в состоянии Free или Done; в него
//     пишется очередная порция PCM, буфер отдаётся через
//     audrvVoiceAddWaveBuf.
//   * 44,1 кГц -> 48 кГц: линейная интерполяция. Для игровых эффектов
//     разница с полноценным ресемплером не слышна, а кода в разы меньше.
//
// Порядок взят из примеров devkitPro (examples/switch/audio/audren-simple и
// hwopus-decoder): audrenInitialize -> audrvCreate -> mempool ->
// audrvUpdate -> audrenStartAudioRenderer -> голоса, далее по кадру
// audrenWaitFrame + audrvUpdate.

#include "flutter_libnx/audio_horizon.h"

#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <malloc.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "flutter_libnx/log.h"

#define MINIMP3_IMPLEMENTATION
// Относительный путь - как у mpv_symbols.inc в static_libraries_horizon.cpp:
// заголовки лежат рядом с исходником вендоренной библиотеки, и добавлять
// include-путь во все четыре Makefile ради двух строк не нужно.
#include "../../../third_party/minimp3/minimp3.h"
#include "../../../third_party/minimp3/minimp3_ex.h"

namespace flutter_libnx {
namespace {

constexpr int kOutputRate = 48000;
// Игра держит до 31 плеера одновременно (audio_service.dart: сумма пулов 30
// плюс bgm), плюс запас на загрузку файла до воспроизведения.
constexpr int kMaxVoices = 32;
constexpr int kSlotsPerVoice = 4;
constexpr int kSrcRate = 44100;
constexpr int kSlotOutSamples = 2304;  // 1152 отсчёта MP3 после растяжения

// Команда из метода канала в аудио-поток. Всё, что может блокировать
// (открытие файла, декодирование), выполняется уже в потоке.
struct Command {
  enum class Kind {
    kOpenSource,
    kResume,
    kPause,
    kStop,
    kVolume,
    kRate,
    kReleaseSource,
  };
  Kind kind;
  int id = 0;
  std::string path;
  float value = 0.0f;
};

// Событие наружу (в Dart). Отправляет платформенный поток при откачке,
// сам аудио-поток в Dart не ходит.
struct AudioEvent {
  int id;
  bool prepared;  // true = audio.onPrepared, false = audio.onComplete
};

struct Voice {
  bool used = false;
  bool started = false;
  bool open = false;
  bool finished = false;
  float volume = 1.0f;
  float rate = 1.0f;
  int channels = 1;

  mp3dec_ex_t decoder{};
  bool decoder_open = false;

  AudioDriverWaveBuf slots[kSlotsPerVoice];

  // Ресемплинг. src_tail хранит последний отсчёт прошлой порции плюс
  // текущую: интерполяция на стыке кадров иначе даёт щелчок.
  std::vector<int16_t> mono_tail;
  double resample_carry = 0.0;

  int64_t out_samples = 0;
};

class AudioEngine {
 public:
  static AudioEngine& Instance() {
    static AudioEngine instance;
    return instance;
  }

  bool Initialize(std::string* error) {
    if (running_.load()) {
      return true;
    }

    static const AudioRendererConfig kConfig = {
        .output_rate = AudioRendererOutputRate_48kHz,
        .num_voices = kMaxVoices,
        .num_effects = 0,
        .num_sinks = 1,
        .num_mix_objs = 1,
        .num_mix_buffers = 2,
    };

    Result res = audrenInitialize(&kConfig);
    if (R_FAILED(res)) {
      *error = "audrenInitialize: " + std::to_string(res);
      return false;
    }
    inited_audren_ = true;

    res = audrvCreate(&driver_, &kConfig, 2);
    if (R_FAILED(res)) {
      *error = "audrvCreate: " + std::to_string(res);
      CloseDriver();
      return false;
    }
    inited_driver_ = true;

    // Mempool: слоты всех голосов подряд. Выравнивание по странице -
    // требование сервиса; сброс кэша - требование процессора к памяти,
    // которую читает DSP.
    const size_t bytes =
        static_cast<size_t>(kMaxVoices) * kSlotsPerVoice * kSlotOutSamples *
        sizeof(int16_t);
    mempool_size_ = (bytes + 0xFFF) & ~static_cast<size_t>(0xFFF);
    mempool_ = memalign(0x1000, mempool_size_);
    if (mempool_ == nullptr) {
      *error = "mempool fuer audren: memalign fehlgeschlagen";
      CloseDriver();
      return false;
    }
    std::memset(mempool_, 0, mempool_size_);
    armDCacheFlush(mempool_, mempool_size_);

    const int mpid = audrvMemPoolAdd(&driver_, mempool_, mempool_size_);
    if (mpid < 0 || !audrvMemPoolAttach(&driver_, mpid)) {
      *error = "audrvMemPoolAdd/Attach fehlgeschlagen";
      CloseDriver();
      return false;
    }

    for (int i = 0; i < kMaxVoices; ++i) {
      Voice& voice = voices_[i];
      const int base = i * kSlotsPerVoice * kSlotOutSamples;
      for (int s = 0; s < kSlotsPerVoice; ++s) {
        AudioDriverWaveBuf& buf = voice.slots[s];
        buf.data_raw = static_cast<uint8_t*>(mempool_) +
                       (base + s * kSlotOutSamples) * sizeof(int16_t);
        buf.size = kSlotOutSamples * sizeof(int16_t);
        buf.start_sample_offset = s * kSlotOutSamples;
        buf.end_sample_offset = buf.start_sample_offset;
      }
    }

    res = audrvUpdate(&driver_);
    if (R_FAILED(res)) {
      *error = "audrvUpdate (Start): " + std::to_string(res);
      CloseDriver();
      return false;
    }
    res = audrenStartAudioRenderer();
    if (R_FAILED(res)) {
      *error = "audrenStartAudioRenderer: " + std::to_string(res);
      CloseDriver();
      return false;
    }

    stop_.store(false);
    running_.store(true);
    thread_ = std::thread([this] { AudioLoop(); });
    LOG_INFO("Audio: audren bereit, %d Stimmen, %d kHz", kMaxVoices,
             kOutputRate);
    return true;
  }

  void Shutdown() {
    if (!running_.exchange(false)) {
      return;
    }
    stop_.store(true);
    if (thread_.joinable()) {
      thread_.join();
    }
    std::lock_guard<std::mutex> guard(mutex_);
    for (Voice& voice : voices_) {
      CloseDecoder(voice);
    }
    CloseDriver();
  }

  int CreatePlayer() {
    std::lock_guard<std::mutex> guard(mutex_);
    for (int i = 0; i < kMaxVoices; ++i) {
      if (!voices_[i].used) {
        Voice& voice = voices_[i];
        voice.used = true;
        voice.started = false;
        voice.open = false;
        voice.finished = false;
        voice.volume = 1.0f;
        voice.rate = 1.0f;
        voice.out_samples = 0;
        voice.resample_carry = 0.0;
        voice.mono_tail.clear();
        return i;
      }
    }
    return -1;
  }

  void ReleasePlayer(int id) {
    Post({Command::Kind::kReleaseSource, id, "", 0.0f});
    std::lock_guard<std::mutex> guard(mutex_);
    voices_[id].used = false;
    voices_[id].open = false;
  }

  void Post(Command command) {
    if (!running_.load()) {
      return;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    commands_.push_back(std::move(command));
  }

  void SetSource(int id, const std::string& path) {
    Post({Command::Kind::kReleaseSource, id, "", 0.0f});
    Post({Command::Kind::kOpenSource, id, path, 0.0f});
  }

  long PositionMs(int id) {
    std::lock_guard<std::mutex> guard(mutex_);
    Voice& voice = voices_[id];
    if (!voice.open) {
      return -1;
    }
    return static_cast<long>(voice.out_samples * 1000 / kOutputRate);
  }

  long DurationMs(int id) {
    std::lock_guard<std::mutex> guard(mutex_);
    Voice& voice = voices_[id];
    if (!voice.decoder_open) {
      return -1;
    }
    const int rate = voice.decoder.info.hz > 0 ? voice.decoder.info.hz : kSrcRate;
    return static_cast<long>(voice.decoder.detected_samples * 1000 / rate);
  }

  bool VoiceIdle(int id) {
    std::lock_guard<std::mutex> guard(mutex_);
    const Voice& voice = voices_[id];
    return !voice.used || !voice.open || voice.finished;
  }

  bool PopEvent(AudioEvent* event) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (events_.empty()) {
      return false;
    }
    *event = events_.front();
    events_.pop_front();
    return true;
  }

 private:
  void CloseDecoder(Voice& voice) {
    if (voice.decoder_open) {
      mp3dec_ex_close(&voice.decoder);
      voice.decoder_open = false;
    }
  }

  void CloseDriver() {
    if (inited_driver_) {
      audrvClose(&driver_);
      inited_driver_ = false;
    }
    if (inited_audren_) {
      audrenExit();
      inited_audren_ = false;
    }
    if (mempool_ != nullptr) {
      free(mempool_);
      mempool_ = nullptr;
    }
  }

  // Единственное место, где трогают драйвер.
  void AudioLoop() {
    while (!stop_.load()) {
      audrenWaitFrame();

      DrainCommands();

      for (int i = 0; i < kMaxVoices; ++i) {
        Voice& voice = voices_[i];
        if (!voice.used || !voice.open || voice.finished) {
          continue;
        }
        if (!audrvVoiceIsPlaying(&driver_, i)) {
          audrvVoiceStart(&driver_, i);
          voice.started = true;
        }
        FillVoice(i, voice);
      }

      const Result res = audrvUpdate(&driver_);
      if (R_FAILED(res)) {
        LOG_ERROR("Audio: audrvUpdate: %d", static_cast<int>(res));
        break;
      }
    }

    for (int i = 0; i < kMaxVoices; ++i) {
      if (inited_driver_ && voices_[i].started) {
        audrvVoiceStop(&driver_, i);
        voices_[i].started = false;
      }
    }
  }

  void DrainCommands() {
    std::deque<Command> pending;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      pending.swap(commands_);
    }
    for (const Command& command : pending) {
      Voice& voice = voices_[command.id];
      switch (command.kind) {
        case Command::Kind::kOpenSource:
          OpenSource(command.id, voice, command.path);
          break;
        case Command::Kind::kResume:
          if (inited_driver_ && voice.started) {
            audrvVoiceSetPaused(&driver_, command.id, false);
          }
          break;
        case Command::Kind::kPause:
          if (inited_driver_ && voice.started) {
            audrvVoiceSetPaused(&driver_, command.id, true);
          }
          break;
        case Command::Kind::kStop:
          StopVoice(command.id, voice);
          break;
        case Command::Kind::kVolume:
          voice.volume = command.value;
          break;
        case Command::Kind::kRate:
          voice.rate = command.value;
          if (inited_driver_ && voice.started) {
            audrvVoiceSetPitch(&driver_, command.id, voice.rate);
          }
          break;
        case Command::Kind::kReleaseSource:
          ReleaseSource(command.id, voice);
          break;
      }
    }
  }

  void OpenSource(int id, Voice& voice, const std::string& path) {
    ReleaseSource(id, voice);

    const int result = mp3dec_ex_open(&voice.decoder, path.c_str(), 0);
    if (result != 0) {
      LOG_ERROR("Audio: MP3 nicht lesbar (%d): %s", result, path.c_str());
      return;
    }
    voice.decoder_open = true;
    voice.channels = voice.decoder.info.channels >= 2 ? 2 : 1;
    voice.open = true;
    voice.finished = false;
    voice.out_samples = 0;
    voice.resample_carry = 0.0;
    voice.mono_tail.clear();

    if (inited_driver_) {
      audrvVoiceInit(&driver_, id, voice.channels, PcmFormat_Int16,
                     voice.decoder.info.hz > 0 ? voice.decoder.info.hz
                                               : kOutputRate);
      audrvVoiceSetDestinationMix(&driver_, id, AUDREN_FINAL_MIX_ID);
      if (voice.channels == 1) {
        // Моно в оба канала - иначе голос слышен только слева.
        audrvVoiceSetMixFactor(&driver_, id, voice.volume, 0, 0);
        audrvVoiceSetMixFactor(&driver_, id, voice.volume, 0, 1);
      } else {
        audrvVoiceSetMixFactor(&driver_, id, voice.volume, 0, 0);
        audrvVoiceSetMixFactor(&driver_, id, voice.volume, 1, 1);
      }
    }

    const int rate = voice.decoder.info.hz > 0 ? voice.decoder.info.hz : kSrcRate;
    LOG_INFO("Audio: Quelle %s, %d Hz, %d Kanal, ~%ld ms", path.c_str(), rate,
             voice.decoder.info.channels,
             static_cast<long>(voice.decoder.detected_samples * 1000 / rate));
    // Готовность - отдельным событием: Dart ждёт именно audio.onPrepared
    // (audioplayer.dart:358), возврата метода ему мало.
    events_.push_back({id, true});
  }

  void ReleaseSource(int id, Voice& voice) {
    CloseDecoder(voice);
    if (inited_driver_ && voice.started) {
      audrvVoiceStop(&driver_, id);
      voice.started = false;
    }
    for (AudioDriverWaveBuf& buf : voice.slots) {
      buf.state = AudioDriverWaveBufState_Free;
    }
    voice.open = false;
    voice.mono_tail.clear();
  }

  void StopVoice(int id, Voice& voice) {
    if (inited_driver_ && voice.started) {
      audrvVoiceStop(&driver_, id);
      voice.started = false;
    }
    for (AudioDriverWaveBuf& buf : voice.slots) {
      buf.state = AudioDriverWaveBufState_Free;
    }
    voice.finished = false;
    voice.out_samples = 0;
  }

  // Добирает в свободный слот очередную порцию PCM.
  void FillVoice(int id, Voice& voice) {
    for (int s = 0; s < kSlotsPerVoice; ++s) {
      AudioDriverWaveBuf& buf = voice.slots[s];
      if (buf.state != AudioDriverWaveBufState_Free &&
          buf.state != AudioDriverWaveBufState_Done) {
        continue;
      }

      // mp3dec_ex_read отдаёт значения с учётом чередования каналов, то есть
      // просим samples * channels int16 на samples отсчётов на канал.
      std::vector<int16_t> raw(kSlotOutSamples * voice.channels);
      const size_t got = mp3dec_ex_read(&voice.decoder, raw.data(),
                                        static_cast<size_t>(kSlotOutSamples) *
                                            voice.channels);
      if (got == 0) {
        voice.finished = true;
        events_.push_back({id, false});  // audio.onComplete
        return;
      }

      // data_raw объявлен как const void*, хотя это наша память: её пишет
      // аудио-поток, а читает DSP. Приводить нужно именно так.
      int16_t* dst = static_cast<int16_t*>(
          const_cast<void*>(static_cast<const void*>(buf.data_raw)));
      int written = 0;
      if (voice.channels == 1) {
        written = Resample(voice, raw.data(), static_cast<int>(got), dst);
      } else {
        // Стерео в моно: усредняем, чтобы голос не стал тише после
        // сведения к одному каналу.
        for (size_t i = 0; i + 1 < got; i += 2) {
          raw[i / 2] = static_cast<int16_t>((raw[i] + raw[i + 1]) / 2);
        }
        written = Resample(voice, raw.data(), static_cast<int>(got / 2), dst);
      }

      armDCacheFlush(const_cast<void*>(static_cast<const void*>(buf.data_raw)),
                     static_cast<size_t>(written) * voice.channels *
                         sizeof(int16_t));
      voice.out_samples += written;
      buf.end_sample_offset =
          buf.start_sample_offset + written * voice.channels;
      audrvVoiceAddWaveBuf(&driver_, id, &buf);
      return;
    }
  }

  // 44,1 кГц -> 48 кГц линейной интерполяцией. Возвращает отсчётов на канал.
  int Resample(Voice& voice, const int16_t* mono, int samples, int16_t* dst) {
    if (samples <= 0) {
      return 0;
    }
    constexpr double kStep = static_cast<double>(kSrcRate) / kOutputRate;

    voice.mono_tail.push_back(mono[0]);
    voice.mono_tail.insert(voice.mono_tail.end(), mono, mono + samples);

    double position = voice.resample_carry;
    int written = 0;
    const int total = static_cast<int>(voice.mono_tail.size());
    while (written < kSlotOutSamples) {
      const int index = static_cast<int>(position);
      if (index + 1 >= total) {
        break;
      }
      const int left = voice.mono_tail[static_cast<size_t>(index)];
      const int right = voice.mono_tail[static_cast<size_t>(index + 1)];
      const double frac = position - index;
      dst[written] = static_cast<int16_t>(left + (right - left) * frac);
      written++;
      position += kStep;
    }
    voice.resample_carry = position - (total - 1);
    if (voice.resample_carry < 0.0) {
      voice.resample_carry = 0.0;
    }

    // Оставляем последний отсчёт для стыка с следующей порцией.
    voice.mono_tail.clear();
    voice.mono_tail.push_back(mono[samples - 1]);
    return written;
  }

  AudioDriver driver_{};
  void* mempool_ = nullptr;
  size_t mempool_size_ = 0;
  bool inited_audren_ = false;
  bool inited_driver_ = false;

  Voice voices_[kMaxVoices];

  std::mutex mutex_;
  std::deque<Command> commands_;
  std::deque<AudioEvent> events_;

  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> running_{false};
};

}  // namespace
}  // namespace flutter_libnx

extern "C" {

bool flutter_libnx_audio_init(std::string* error) {
  return flutter_libnx::AudioEngine::Instance().Initialize(error);
}

void flutter_libnx_audio_shutdown() {
  flutter_libnx::AudioEngine::Instance().Shutdown();
}

int flutter_libnx_audio_create_player() {
  return flutter_libnx::AudioEngine::Instance().CreatePlayer();
}

void flutter_libnx_audio_release_player(int id) {
  flutter_libnx::AudioEngine::Instance().ReleasePlayer(id);
}

bool flutter_libnx_audio_set_source(int id, const std::string& path,
                                    std::string* error) {
  if (id < 0 || id >= flutter_libnx::kMaxVoices) {
    *error = "unbekannte Player-Id";
    return false;
  }
  flutter_libnx::AudioEngine::Instance().SetSource(id, path);
  return true;
}

void flutter_libnx_audio_play(int id) {
  flutter_libnx::AudioEngine::Instance().Post(
      {flutter_libnx::Command::Kind::kResume, id, "", 0.0f});
}

void flutter_libnx_audio_pause(int id) {
  flutter_libnx::AudioEngine::Instance().Post(
      {flutter_libnx::Command::Kind::kPause, id, "", 0.0f});
}

void flutter_libnx_audio_stop(int id) {
  flutter_libnx::AudioEngine::Instance().Post(
      {flutter_libnx::Command::Kind::kStop, id, "", 0.0f});
}

void flutter_libnx_audio_set_volume(int id, float volume) {
  flutter_libnx::AudioEngine::Instance().Post(
      {flutter_libnx::Command::Kind::kVolume, id, "", volume});
}

void flutter_libnx_audio_set_rate(int id, float rate) {
  flutter_libnx::AudioEngine::Instance().Post(
      {flutter_libnx::Command::Kind::kRate, id, "", rate});
}

void flutter_libnx_audio_release_source(int id) {
  flutter_libnx::AudioEngine::Instance().Post(
      {flutter_libnx::Command::Kind::kReleaseSource, id, "", 0.0f});
}

long flutter_libnx_audio_position_ms(int id) {
  return flutter_libnx::AudioEngine::Instance().PositionMs(id);
}

long flutter_libnx_audio_duration_ms(int id) {
  return flutter_libnx::AudioEngine::Instance().DurationMs(id);
}

bool flutter_libnx_audio_voice_idle(int id) {
  return flutter_libnx::AudioEngine::Instance().VoiceIdle(id);
}

bool flutter_libnx_audio_pop_event(int* id, bool* prepared) {
  flutter_libnx::AudioEvent event;
  if (!flutter_libnx::AudioEngine::Instance().PopEvent(&event)) {
    return false;
  }
  *id = event.id;
  *prepared = event.prepared;
  return true;
}

}  // extern "C"