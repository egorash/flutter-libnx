// Аудио-движок поверх libnx audren.
//
// Зачем отдельный файл: приложение общается с аудио не напрямую, а через
// канал плагина audioplayers. Всё, что ниже этого заголовка, — деталь
// реализации: собственный поток, голоса audren, декодер и ресемплинг.
//
// Источник данных — файлы на SD-карте. Dart-сторона плагина сама
// распаковывает ассет во временный файл (audioplayers/audio_cache.dart:100
// через path_provider), поэтому открывать RomFS здесь не нужно.
//
// Особенность, которую нельзя упустить: audren умеет только 32 и 48 кГц,
// а ассеты игры — 44,1 кГц (44100). Отсюда ресемплинг на каждом голосе;
// без него звук был бы на 8,8 % ниже и выше по тону.
#pragma once

#include <string>

#include "embedder.h"

extern "C" {

// Поднимает audren и запускает аудио-поток. false с текстом ошибки, если
// сервис недоступен: без звука приложение всё равно работает.
bool flutter_libnx_audio_init(std::string* error);

// Останавливает поток и освобождает audren. Вызывать до выхода из main.
void flutter_libnx_audio_shutdown();

// --- Управление игроками ---------------------------------------------------
// Игрок соответствует одному AudioPlayer в Dart. Идентификатор выдаёт
// create и действителен до release_player.

// Новый игрок. -1, если свободных голосов нет.
int flutter_libnx_audio_create_player();

// Отвязывает игрока от голоса и закрывает файл. Голос освобождается.
void flutter_libnx_audio_release_player(int id);

// Открывает файл и готовит голос. Готовность сообщается событием
// audio.onPrepared, а не возвратом: этого ждёт Dart (audioplayer.dart:358).
bool flutter_libnx_audio_set_source(int id, const std::string& path,
                                    std::string* error);

void flutter_libnx_audio_play(int id);
void flutter_libnx_audio_pause(int id);

// Останавливает и перематывает в начало.
void flutter_libnx_audio_stop(int id);

void flutter_libnx_audio_set_volume(int id, float volume);
void flutter_libnx_audio_set_rate(int id, float rate);

// Закрывает файл, голос остаётся за игроком (так требует протокол:
// release закрывает ресурс, dispose удаляет игрока).
void flutter_libnx_audio_release_source(int id);

// Позиция и длительность в миллисекундах; -1, если голос не готов.
long flutter_libnx_audio_position_ms(int id);
long flutter_libnx_audio_duration_ms(int id);

// Забирает накопленное аудио-потоком событие. Вызывать из главного цикла на
// платформенном потоке: в Dart события уходят через EventChannel, а тот
// требует именно этого потока. prepared = audio.onPrepared,
// !prepared = audio.onComplete.
bool flutter_libnx_audio_pop_event(int* id, bool* prepared);

}  // extern "C"