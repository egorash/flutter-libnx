// Проба без Flutter и без Dart: что консоль отдаёт на самом деле.
//
// Зачем она нужна. Пять итераций правок в плагине дали логи, которые
// выглядели правдоподобно, но ничего не прояснили. Причина метода: не было
// базовой линии. Неизвестно было, работает ли audren на этой консоли вообще,
// и неизвестно было, какие биты отдаёт ввод в портативном режиме - а
// раскладку приходилось выводить из заголовков, а не из измерения.
//
// Эта программа отвечает на оба вопроса напрямую, без приложения:
//
//   1. Звук. Синус 440 Гц через audren - раскладка буферов в точности как в
//      эталоне hwopus-decoder, темп задаёт audrenGetFrameEvent(). Раз в
//      секунду печатается, сколько отсчётов сервис реально отдал. Если счётчик
//      не растёт, а форма волны на экране есть, то дело не в нашем коде
//      микшера. Если растёт и звука нет - искать вывод системы.
//
//   2. Ввод. При каждом изменении печатается СЫРАЯ маска кнопок в
//      шестнадцатеричном виде вместе со стиками. Никакой раскладки, никаких
//      имён: сначала нужно увидеть, что приходит, и только потом решать,
//      во что это превращать.
//
// Ни Engine, ни Dart, ни плагина: сборка занимает секунды, и провал ничего
// кроме самой пробы не задевает.

#include <switch.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "flutter_libnx/log.h"
#include "flutter_libnx/tick_clock.h"

namespace {

// --- Тон ---------------------------------------------------------------

constexpr int kOutputRate = 48000;
constexpr int kChannelCount = 2;
constexpr int kVoiceCount = 8;
constexpr int kSlotSamples = 2400;   // 10 кадров по 240 отсчётов
constexpr int kSlotsPerVoice = 4;
constexpr int kFreqHz = 440;

// --- Ввод --------------------------------------------------------------

constexpr int kPollHz = 60;
// In TICKS. armGetSystemTick() liefert Ticks, nicht Nanosekunden - mit
// "1000000000 / kPollHz" lag zwischen zwei Abfragen 0,868 s statt 16,7 ms.
const u64 kPollPeriodTicks = flutter_libnx::UsToTick(1000000 / kPollHz);

AudioDriver g_driver;

// Achtung: armGetSystemTick() gibt Ticks zurueck (19,2 MHz), nicht
// Nanosekunden. Alles, was ms rechnet, muss ueber TickToMs laufen.
u64 now_ticks() {
  return flutter_libnx::TickNow();
}

// --- Тон: синус в буфере, буфер у голоса -------------------------------

// Заполняет один слот голоса синусоидой. Смещение фазы хранится в счётчике
// голоса, поэтому сигнал непрерывен на стыках порций.
void fill_sine(AudioDriverWaveBuf* buf, int voice, int slot, u64* phase) {
  const int start = slot * kSlotSamples;
  int16_t* dst = reinterpret_cast<int16_t*>(const_cast<void*>(buf->data_raw));
  for (int i = 0; i < kSlotSamples; ++i) {
    const double t = (static_cast<double>(start + i) / kOutputRate) * kFreqHz;
    const int16_t v = static_cast<int16_t>(std::sin(t * 2.0 * M_PI) * 20000.0);
    dst[i] = v;
    dst[i + kSlotSamples] = v;  // моно в оба канала, как в примере
  }
  *phase += kSlotSamples;

  buf->start_sample_offset = start;
  buf->end_sample_offset = start + kSlotSamples;
  armDCacheFlush(const_cast<void*>(static_cast<const void*>(buf->data_raw)),
                 static_cast<size_t>(kSlotSamples) * kChannelCount *
                     sizeof(int16_t));
}

// --- Расшифровка кнопок ------------------------------------------------
//
// Не украшение, а смысл пробы: пока в лог падает голое «0x004033c2», приходится
// держать в голове значения констант libnx. Имя рядом с числом превращает
// догадку в измерение. Отдельно помечены псевдокнопки стиков (биты 16..27) -
// libnx выставляет их САМ, когда стик отклонён, и это самая частая причина
// «нажато что-то непонятное».
struct ButtonName {
  u64 mask;
  const char* name;
  bool pseudo;
};

constexpr ButtonName kButtonNames[] = {
    {HidNpadButton_Left, "Left", false},
    {HidNpadButton_Right, "Right", false},
    {HidNpadButton_Down, "Down", false},
    {HidNpadButton_Up, "Up", false},
    {HidNpadButton_StickL, "StickL", false},
    {HidNpadButton_StickR, "StickR", false},
    {HidNpadButton_L, "L", false},
    {HidNpadButton_R, "R", false},
    {HidNpadButton_ZL, "ZL", false},
    {HidNpadButton_ZR, "ZR", false},
    {HidNpadButton_Plus, "Plus", false},
    {HidNpadButton_Minus, "Minus", false},
    // SL/SR haben im Handheld-Betrieb je eine Fassung pro Joy-Con.
    {HidNpadButton_LeftSL, "LeftSL", false},
    {HidNpadButton_LeftSR, "LeftSR", false},
    {HidNpadButton_RightSL, "RightSL", false},
    {HidNpadButton_RightSR, "RightSR", false},
    {HidNpadButton_A, "A", false},
    {HidNpadButton_B, "B", false},
    {HidNpadButton_X, "X", false},
    {HidNpadButton_Y, "Y", false},
    {HidNpadButton_StickLLeft, "StickLLeft", true},
    {HidNpadButton_StickLUp, "StickLUp", true},
    {HidNpadButton_StickLRight, "StickLRight", true},
    {HidNpadButton_StickLDown, "StickLDown", true},
    {HidNpadButton_StickRLeft, "StickRLeft", true},
    {HidNpadButton_StickRUp, "StickRUp", true},
    {HidNpadButton_StickRRight, "StickRRight", true},
    {HidNpadButton_StickRDown, "StickRDown", true},
};

// Пишет список нажатых кнопок через «+» в dest. Само по себе это ничего не
// меняет - только текст в протоколе.
void describe_buttons(u64 mask, char* dest, size_t size) {
  dest[0] = '\0';
  size_t used = 0;
  for (const ButtonName& entry : kButtonNames) {
    if ((mask & entry.mask) == 0) {
      continue;
    }
    const int written = std::snprintf(dest + used, size - used, "%s%s%s",
                                      used > 0 ? "+" : "",
                                      entry.pseudo ? "~" : "", entry.name);
    if (written < 0 || static_cast<size_t>(written) >= size - used) {
      break;
    }
    used += static_cast<size_t>(written);
  }
  if (used == 0) {
    std::snprintf(dest, size, "keine");
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  consoleInit(nullptr);

  flutter_libnx::LogConfig log_config;
  log_config.to_nxlink = false;
  log_config.file_path = "sdmc:/switch/flutter-libnx/hw_probe.log";
  flutter_libnx::LogInit(log_config);

  std::printf("\n  flutter-libnx / HW-Probe\n");
  std::printf("  ==========================\n\n");
  std::printf("  Laeuft 5 Minuten. Bitte:\n\n");
  std::printf("    1) HAENGEN WOBEN, ob der Sinus zu hoeren ist\n");
  std::printf("    2) JEDE Taste einzeln druecken und wieder loslassen\n");
  std::printf("    3) Beide Sticks bewegen\n\n");
  std::printf("  Protokoll: sdmc:/switch/flutter-libnx/hw_probe.log\n\n");
  consoleUpdate(nullptr);

  LOG_INFO("=== hw_probe: ohne Flutter, ohne Dart ===");

  // ---------------- Ввод ----------------

  PadState pad = {};
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);

  LOG_INFO("Eingabe: fragt ab jetzt alle %d ms ab", 1000 / kPollHz);

  // ---------------- Ton ----------------

  static const AudioRendererConfig ren_config = {
      .output_rate = AudioRendererOutputRate_48kHz,
      .num_voices = kVoiceCount,
      .num_effects = 0,
      .num_sinks = 1,
      .num_mix_objs = 1,
      .num_mix_buffers = 2,
  };

  Result res = audrenInitialize(&ren_config);
  if (R_FAILED(res)) {
    LOG_ERROR("audrenInitialize: %d", static_cast<int>(res));
    return 1;
  }
  res = audrvCreate(&g_driver, &ren_config, kChannelCount);
  if (R_FAILED(res)) {
    LOG_ERROR("audrvCreate: %d", static_cast<int>(res));
    audrenExit();
    return 1;
  }

  const size_t bytes = static_cast<size_t>(kVoiceCount) * kSlotsPerVoice *
                       kSlotSamples * kChannelCount * sizeof(int16_t);
  const size_t pool_size = (bytes + 0xFFF) & ~static_cast<size_t>(0xFFF);
  void* pool = memalign(0x1000, pool_size);
  if (pool == nullptr) {
    LOG_ERROR("memalign fuer mempool: %zu Bytes fehlgeschlagen", pool_size);
    return 1;
  }
  std::memset(pool, 0, pool_size);
  armDCacheFlush(pool, pool_size);

  const int mpid = audrvMemPoolAdd(&g_driver, pool, pool_size);
  if (mpid < 0 || !audrvMemPoolAttach(&g_driver, mpid)) {
    LOG_ERROR("audrvMemPoolAdd/Attach: %d", mpid);
    return 1;
  }

  // OHNE diese Zeile bleibt die Endmischung ohne Ziel und es ist still.
  // Sie steht in audren-simple, fehlte aber sowohl hier als auch im Plugin.
  static const u8 sink_channels[kChannelCount] = {0, 1};
  const int sink = audrvDeviceSinkAdd(&g_driver, AUDREN_DEFAULT_DEVICE_NAME,
                                      kChannelCount, sink_channels);
  if (sink < 0) {
    LOG_ERROR("audrvDeviceSinkAdd: %d", sink);
    return 1;
  }
  LOG_INFO("Ausgang an '%s', %d Kanaele, Senke %d", AUDREN_DEFAULT_DEVICE_NAME,
           kChannelCount, sink);

  static AudioDriverWaveBuf slots[kVoiceCount][kSlotsPerVoice];
  for (int v = 0; v < kVoiceCount; ++v) {
    for (int s = 0; s < kSlotsPerVoice; ++s) {
      AudioDriverWaveBuf& buf = slots[v][s];
      buf.data_raw = static_cast<uint8_t*>(pool) +
                     (v * kSlotsPerVoice * kSlotSamples) * kChannelCount *
                         sizeof(int16_t);
      buf.size = kSlotsPerVoice * kSlotSamples * kChannelCount * sizeof(int16_t);
      buf.start_sample_offset = s * kSlotSamples;
      buf.end_sample_offset = buf.start_sample_offset;
    }
  }

  if (!audrvVoiceInit(&g_driver, 0, 1, PcmFormat_Int16, kOutputRate)) {
    LOG_ERROR("audrvVoiceInit(0): abgelehnt");
    return 1;
  }
  audrvVoiceSetDestinationMix(&g_driver, 0, AUDREN_FINAL_MIX_ID);
  audrvVoiceSetMixFactor(&g_driver, 0, 1.0f, 0, 0);
  audrvVoiceSetMixFactor(&g_driver, 0, 1.0f, 0, 1);
  audrvVoiceStart(&g_driver, 0);

  res = audrenStartAudioRenderer();
  if (R_FAILED(res)) {
    LOG_ERROR("audrenStartAudioRenderer: %d", static_cast<int>(res));
    return 1;
  }
  LOG_INFO("Ton: 440 Hz auf einer Stimme, Takt ueber audrenGetFrameEvent()");

  // ---------------- Schleife ----------------

  u64 buttons_prev = 0;
  u64 next_poll = 0;
  u64 next_report = 0;
  u64 phase = 0;
  u64 samples_in = 0;

  // Свои часы вместо appletMainLoop(): проба - измерительный прибор, а не
  // приложение. Пять минут гарантированно, независимо от того, когда ее
  // закрыли, - иначе не набирается ни одной секундной сводки, ради которых
  // все и затевалось. B переводит в цикл сводки чаще, но не останавливает.
  const u64 probe_end = now_ticks() + flutter_libnx::UsToTick(300000000);
  while (now_ticks() < probe_end) {
    audrenWaitFrame();
    const u64 now = now_ticks();

    // Ввод: только изменения, сырые биты.
    if (now >= next_poll) {
      next_poll = now + kPollPeriodTicks;
      padUpdate(&pad);
      if (pad.buttons_cur != buttons_prev) {
        char names[192];
        describe_buttons(pad.buttons_cur, names, sizeof(names));
        LOG_INFO("Eingabe: 0x%08llx -> 0x%08llx [%s] Stick L %d/%d R %d/%d",
                 static_cast<unsigned long long>(buttons_prev),
                 static_cast<unsigned long long>(pad.buttons_cur), names,
                 pad.sticks[0].x, pad.sticks[0].y, pad.sticks[1].x,
                 pad.sticks[1].y);
        buttons_prev = pad.buttons_cur;
      }
    }

    // Ton: JEDEN freien Puffer fuellen. Die erste Fassung sah nur auf Slot 0
    // und fuellte deshalb genau einen - nach 50 ms war Schluss, die Stimme
    // hungerte. Es ist ausserdem wichtig, dass hier mehrere Puffer hintereinander
    // in der Schlange liegen: genau daran sieht man, ob der Dienst sie
    // abnimmt und weiterreicht.
    for (int s = 0; s < kSlotsPerVoice; ++s) {
      if (slots[0][s].state != AudioDriverWaveBufState_Free &&
          slots[0][s].state != AudioDriverWaveBufState_Done) {
        continue;
      }
      fill_sine(&slots[0][s], 0, s, &phase);
      samples_in += kSlotSamples;
      if (!audrvVoiceAddWaveBuf(&g_driver, 0, &slots[0][s])) {
        LOG_ERROR("Ton: Puffer %d abgelehnt (Zustand %d)", s,
                  static_cast<int>(slots[0][s].state));
      }
      break;  // je Takt eine Portion, sonst laeuft der Puffer ueber
    }
    if (!audrvVoiceIsPlaying(&g_driver, 0)) {
      audrvVoiceStart(&g_driver, 0);
    }

    const Result up = audrvUpdate(&g_driver);
    if (R_FAILED(up)) {
      LOG_ERROR("audrvUpdate: %d", static_cast<int>(up));
      break;
    }

    if (now >= next_report) {
      // 100 ms statt 1 s: auch ein zehnsekündiger Lauf liefert dann
      // hundert Datenpunkte - sonst war die Probe bei kurzem Lauf wertlos.
      next_report = now + flutter_libnx::UsToTick(100000);
      char states[kSlotsPerVoice * 8 + 1] = {};
      for (int s = 0; s < kSlotsPerVoice; ++s) {
        char one[8] = {};
        std::snprintf(one, sizeof(one), "%d,",
                      static_cast<int>(slots[0][s].state));
        std::strncat(states, one, sizeof(states) - std::strlen(states) - 1);
      }
      LOG_INFO("Ton: unsere %llu, ABGESPIELT %u, verworfen %u, Puffer %s",
               static_cast<unsigned long long>(samples_in),
               audrvVoiceGetPlayedSampleCount(&g_driver, 0),
               audrvVoiceGetVoiceDropsCount(&g_driver, 0), states);
    }
  }

  LOG_INFO("hw_probe beendet");
  audrvClose(&g_driver);
  audrenExit();
  free(pool);
  return 0;
}