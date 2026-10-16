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
#include "flutter_libnx/tick_clock.h"

#define MINIMP3_IMPLEMENTATION
// Относительный путь - как у mpv_symbols.inc в static_libraries_horizon.cpp:
// заголовки лежат рядом с исходником вендоренной библиотеки, и добавлять
// include-путь во все четыре Makefile ради двух строк не нужно.
#include "../../../third_party/minimp3/minimp3.h"
#include "../../../third_party/minimp3/minimp3_ex.h"

namespace flutter_libnx {
namespace {

constexpr int kOutputRate = 48000;
// Каналов конечной шины. Совпадает с num_mix_buffers в конфиге и со вторым
// аргументом audrvCreate.
constexpr int kOutputChannels = 2;
// Игра держит до 31 плеера одновременно (audio_service.dart: сумма пулов 30
// плюс bgm), плюс запас на загрузку файла до воспроизведения.
constexpr int kMaxVoices = 32;
// Трассировка микшера (FLUTTER_LIBNX_AUDIO_TRACE) выключена по умолчанию:
// она нужна ровно тогда, когда звук не слышен, и больше только засоряет
// протокол. Полностью выбрасывать её нельзя - именно эти строки показали,
// что при fehlender Senke der Dienst null Abtastwerte ausgibt, obwohl die
// Daten ankommen, und dass das Reseampeln 8,1 % verliert.
//
// Блоки ниже помечены #ifdef, а не if constexpr: поля счётчиков тоже под
// #ifdef, а if constexpr компилятор отбрасывает лишь после разбора имён -
// он не смог бы даже собрать обращение к несуществующему полю.
constexpr int kSlotsPerVoice = 4;
constexpr int kSrcRate = 44100;
// Кадр рендерера при 48 кГц - AUDREN_SAMPLES_PER_FRAME_48KHZ отсчётов, и
// сервис ждёт буферы, кратные этому шагу. 2400 = 10 кадров = 50 мс.
constexpr int kSlotOutSamples = 2400;
// 44100/48000 = 147/160 ровно. Ресемплинг идёт целыми числами в единицах
// 1/160 отсчёта: накопление ошибки в double за сотни тысяч отсчётов даёт
// щелчки на стыках порций.
constexpr int kSrcStep160 = 147;   // на сколько сотых отсчёта шагаем за выход
constexpr int kScale160 = 160;
// Сколько входных отсчётов нужно на одну порцию: out * 44100 / 48000.
// При 2400 выходных ровно 2205 входных - и перенос дробной части между
// порциями равен нулю, то есть ничего не приходится обрезать.
constexpr int kSlotInSamples = (kSlotOutSamples * kSrcRate) / kOutputRate;

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
  int resample_carry160 = 0;

  int64_t out_samples = 0;
  // Уже отправлено ли audio.onComplete для этого голоса.
  bool reported_done = false;

#ifdef FLUTTER_LIBNX_AUDIO_TRACE
  // Нужны только трассировке, поэтому в обычной сборке их нет вовсе: не
  // место платить за них памятью на 32 голоса.
  // Сколько порций уже показали в лог: трёх с первых по каждому голосу
  // хватает, чтобы отличить "декодер идёт" от "декодер молчит".
  int log_slots = 0;
  // Состояние последней сводки: сколько отсчётов сервис уже отдал.
  u32 last_played = 0;
  bool reported = false;
#endif
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
        .num_mix_buffers = kOutputChannels,
    };

    Result res = audrenInitialize(&kConfig);
    if (R_FAILED(res)) {
      *error = "audrenInitialize: " + std::to_string(res);
      return false;
    }
    inited_audren_ = true;

    res = audrvCreate(&driver_, &kConfig, kOutputChannels);
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

    // Выход к устройству. Без этой строки у конечной шины нет адресата:
    // _audrvInitConfig в libnx заполняет только etc-копию массива стоков,
    // а d->in_sinks[0] остаётся нулевым, то есть type = Invalid и
    // is_used = false. Рендерер тогда считает в пустоту: отсчёты уходят,
    // played_sample_count остаётся нулём, звука нет. Строка есть в эталоне
    // audren-simple - и именно её здесь не хватало.
    static const u8 kSinkChannels[kOutputChannels] = {0, 1};
    const int sink = audrvDeviceSinkAdd(&driver_, AUDREN_DEFAULT_DEVICE_NAME,
                                        kOutputChannels, kSinkChannels);
    if (sink < 0) {
      *error = "audrvDeviceSinkAdd fehlgeschlagen";
      CloseDriver();
      return false;
    }
    LOG_INFO("Audio: Ausgang an '%s', %d Kanaele, Senke %d",
             AUDREN_DEFAULT_DEVICE_NAME, kOutputChannels, sink);

    for (int i = 0; i < kMaxVoices; ++i) {
      Voice& voice = voices_[i];
      // Раскладка ровно как в hwopus-decoder: data_raw указывает на НАЧАЛО
      // области голоса, а слоты адресуются смещениями. Смещения измеряются
      // в ОТСЧЁТАХ (элементах int16), а size - в байтах.
      //
      // Прошлая версия давала каждому слоту свой data_raw и брала смещение в
      // байтах - сервис читал вдвое больше, чем в буфере есть, и голос
      // молчал: played_sample_count оставался нулём.
      for (int s = 0; s < kSlotsPerVoice; ++s) {
        AudioDriverWaveBuf& buf = voice.slots[s];
        buf.data_raw = static_cast<uint8_t*>(mempool_) +
                       (i * kSlotsPerVoice * kSlotOutSamples) * sizeof(int16_t);
        buf.size = kSlotsPerVoice * kSlotOutSamples * sizeof(int16_t);
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
        voice.resample_carry160 = 0;
        voice.mono_tail.clear();
        // Голос берётся из пула повторно, поэтому и счётчики для протокола
        // надо сбросить - иначе сводка по нему больше не появится.
        voice.reported_done = false;
#ifdef FLUTTER_LIBNX_AUDIO_TRACE
        voice.log_slots = 0;
        voice.last_played = 0;
        voice.reported = false;
#endif
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
    // Темп задаёт сам audrenWaitFrame() - так делает эталон audren-simple,
    // и так же надо здесь.
    //
    // Предыдущая правка ставила сюда audrenGetFrameEvent(), потому что
    // поврерила, будто цикл не ограничен по скорости. Вывод был ложным:
    // время в протоколе считалось как тики, делённые на 1'000'000, то есть
    // занижено в 52 раза. Реально 19 секунд музыки декодировались за 31
    // секунду - нормальный темп. Ничего чинить там было не нужно.
    u64 next_drops_report = 0;
    while (!stop_.load()) {
      audrenWaitFrame();
      [[maybe_unused]] const u64 now = flutter_libnx::TickNow();

      DrainCommands();

      for (int i = 0; i < kMaxVoices; ++i) {
        Voice& voice = voices_[i];
        if (!voice.used || !voice.open) {
          continue;
        }
        if (voice.finished) {
          // Декодировать больше нечего. Ждём, пока рендерер выдаст всё, что
          // уже лежит в буферах, и только тогда сообщаем о завершении.
          if (!voice.reported_done && !audrvVoiceIsPlaying(&driver_, i)) {
            voice.reported_done = true;
            std::lock_guard<std::mutex> guard(mutex_);
            events_.push_back({i, false});  // audio.onComplete
          }
          continue;
        }
        // Erst Daten herein, dann starten. Umgekehrt startet die Stimme leer
        // und laeuft sofort wieder leer an - das war die Reihenfolge
        // zuerst, und danach war nichts hoerbar.
        FillVoice(i, voice);
        if (!audrvVoiceIsPlaying(&driver_, i)) {
          audrvVoiceStart(&driver_, i);
          voice.started = true;
        }
      }

      const Result res = audrvUpdate(&driver_);
      if (R_FAILED(res)) {
        LOG_ERROR("Audio: audrvUpdate: %d", static_cast<int>(res));
        break;
      }

      // Wie viele Abtastwerte der Dienst wirklich ausgegeben hat. Ohne
      // diese Zeile ist "kein Ton" von "Daten kommen an" nicht zu
      // unterscheiden - genau diese Verwechslung hat fuenf Runden gekostet.
#ifdef FLUTTER_LIBNX_AUDIO_TRACE
      {
        if (now >= next_drops_report) {
          next_drops_report = now + flutter_libnx::UsToTick(1000000);
          for (int i = 0; i < kMaxVoices; ++i) {
            Voice& voice = voices_[i];
            if (!voice.used || !voice.open || voice.finished) {
              continue;
            }
            const u32 played = audrvVoiceGetPlayedSampleCount(&driver_, i);
            const u32 drops = audrvVoiceGetVoiceDropsCount(&driver_, i);
            if (!voice.reported || played != voice.last_played) {
              voice.reported = true;
              voice.last_played = played;
              char states[kSlotsPerVoice * 3 + 1] = {};
              for (int s = 0; s < kSlotsPerVoice; ++s) {
                char one[4] = {};
                std::snprintf(one, sizeof(one), "%d,",
                              static_cast<int>(voice.slots[s].state));
                std::strncat(states, one,
                             sizeof(states) - std::strlen(states) - 1);
              }
              LOG_INFO("Audio: Stimme %d - unsere %lld, abgespielt %u, "
                       "verworfen %u, Puffer %s",
                       i, static_cast<long long>(voice.out_samples), played,
                       drops, states);
            }
          }
        }
      }
#endif
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
    voice.reported_done = false;
    voice.out_samples = 0;
    voice.resample_carry160 = 0;
    voice.mono_tail.clear();

    if (inited_driver_) {
      // Частота голоса - всегда частота рендера, а не источника. Данные после
      // ресемплинга уже в 48 кГц; дополнительно сообщать частоту файла
      // означало бы посчитать понижение дважды - ровно та путаница между
      // кадрами источника и выхода, из-за которой в horizon-wine
      // (PR autorun#69) понадобились отдельные счётчики.
      if (!audrvVoiceInit(&driver_, id, voice.channels, PcmFormat_Int16,
                          kOutputRate)) {
        LOG_ERROR("Audio: Stimme %d liess sich nicht einrichten (%d Kanal)", id,
                  voice.channels);
      }
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
    std::lock_guard<std::mutex> guard(mutex_);
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
      // Просим ровно столько входных отсчётов, сколько нужно на порцию.
      // Раньше бралось kSlotOutSamples, то есть на 195 лишних, которые потом
      // молча выбрасывались вместе с куском сигнала.
      std::vector<int16_t> raw(kSlotInSamples * voice.channels);
      const size_t got = mp3dec_ex_read(&voice.decoder, raw.data(),
                                        static_cast<size_t>(kSlotInSamples) *
                                            voice.channels);
      if (got == 0) {
        // Декодер дочитал файл. Но это НЕ значит, что звук прозвучал: у
        // голоса в рендерере ещё лежит около 170 мс уже декодированного.
        // Событие завершения отправляем ниже, когда очередь действительно
        // опустеет - иначе голос освобождается на середине звука, и хвост
        // обрывается.
        voice.finished = true;
        LOG_INFO("Audio: Stimme %d zu Ende, %lld Abtastwerte gesamt", id,
                 static_cast<long long>(voice.out_samples));
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
#ifdef FLUTTER_LIBNX_AUDIO_TRACE
      {
        if (voice.log_slots < 3) {
          voice.log_slots++;
          // Размах данных в слоте. Ноль означал бы, что виноват декодер или
          // ресемплинг; ненулевое значение при полной тишине означало бы, что
          // данные на месте, а теряются они уже на выходе.
          int peak = 0;
          for (int i = 0; i < written; ++i) {
            const int magnitude = dst[i] < 0 ? -dst[i] : dst[i];
            if (magnitude > peak) {
              peak = magnitude;
            }
          }
          LOG_INFO("Audio: Stimme %d -> %d Abtastwerte in Slot %d (Spitze %d)",
                   id, written, s, peak);
        }
      }
#endif
      buf.start_sample_offset = s * kSlotOutSamples;
      buf.end_sample_offset = buf.start_sample_offset + written;
      if (!audrvVoiceAddWaveBuf(&driver_, id, &buf)) {
        LOG_ERROR("Audio: Stimme %d, Slot %d abgelehnt (Zustand %d)", id, s,
                  static_cast<int>(buf.state));
      }
      return;
    }
  }

  // 44,1 кГц -> 48 кГц линейной интерполяцией. Возвращает отсчётов на канал.
  //
  // Позиция живёт в сотых долях отсчёта (carry160), чтобы не копить ошибку.
  // Обнулять отрицательный перенос нельзя: отрицательный перенос означает,
  // что входных отсчётов в порции больше, чем нужно на выход, и хвост пришлось
  // бы выбросить. Раньше это происходило с каждым слотом - терялось 195
  // отсчётов из 2400, то есть 8,1 % сигнала. На слух это две поломки сразу:
  // данные 44,1 кГц игрались как 48 кГц (тон выше и быстрее на 8,8 %, «сжатый»
  // звук) и каждые 54 мс пропадал кусок в 4,4 мс (прерывистость).
  int Resample(Voice& voice, const int16_t* mono, int samples, int16_t* dst) {
    if (samples <= 0) {
      return 0;
    }

    voice.mono_tail.push_back(mono[0]);
    voice.mono_tail.insert(voice.mono_tail.end(), mono, mono + samples);

    int carry160 = voice.resample_carry160;
    int written = 0;
    const int total = static_cast<int>(voice.mono_tail.size());
    while (written < kSlotOutSamples) {
      const int index = carry160 / kScale160;
      if (index + 1 >= total || index < 0) {
        break;
      }
      const int left = voice.mono_tail[static_cast<size_t>(index)];
      const int right = voice.mono_tail[static_cast<size_t>(index + 1)];
      const int frac = carry160 % kScale160;
      dst[written] =
          static_cast<int16_t>(left + (right - left) * frac / kScale160);
      written++;
      carry160 += kSrcStep160;
    }

    // Следующая порция начинается с последнего отсчёта текущей, поэтому
    // позицию сдвигаем на него. При штатном размере слота результат равен
    // нулю; при коротком последнем куске сохраняет дробную часть.
    const int shift = (total - 1) * kScale160;
    voice.resample_carry160 = carry160 - shift;
    if (voice.resample_carry160 < 0) {
      // Порция входных короче, чем нужно: держим позицию у её конца.
      voice.resample_carry160 = 0;
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