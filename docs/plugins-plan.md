# План по плагинам

Статус: черновик. Реализация отложена. Дата: 2026-10-11.
Ветка: master (дефолт форка egorash/flutter-libnx).

Единый план по плагинам `my_nexus`. Только то, что касается плагинов.

## 1. Источники
- `docs/plugin-plan-my-nexus.md` — фазы 0–4
- `docs/webview-plan.md` — webview_flutter (этапы 1–6)
- `docs/album-browser-plan.md` — браузер фото из Альбома (ветка `album-picker`, PR #2)

## 2. Уже реализовано
| Плагин | Канал | Примечание |
|---|---|---|
| `path_provider` | `plugins.flutter.io/path_provider` | Standard |
| `shared_preferences` | `plugins.flutter.io/shared_preferences` | Standard |
| `flutter_secure_storage` | `plugins.it_nomads.com/flutter_secure_storage` | Standard |
| `url_launcher` | `plugins.flutter.io/url_launcher` | Standard (ждёт ретеста) |
| `file_picker` | `miguelruivo.flutter.plugins.filepicker` | JSON |
| `textinput` (swkbd) | — | `textinput_horizon.cpp` |
| `media_kit_video` | — | `media_kit_video_horizon.cpp` |
| sqlite3 FFI | — | `static_libraries_horizon.cpp` |

## 3. Приоритет реализации
| Приоритет | Плагин | Статус | Заметка |
|---|---|---|---|
| P0 | `geolocator` | реализован (9f8ae8a), ждёт прогона | EventChannel + честный `denied` |
| P1 | `image_picker` (только Альбом) + браузер фото | план есть | Архитектура A/B не выбрана |
| P2 | `webview_flutter` | план есть, реализация не начата | Этапы 1–6 |
| P3 | `image_cropper` | не реализован | после P1 |
| P4 | `url_launcher` | реализован, ретест | 0 вызовов в Фазе 0 |
| P5 | `app_links` | пропущен осознанно | ловится приложением |
| P6 | `connectivity_plus`, `package_info_plus` | игнор | только при реальных вызовах |

## 4. Инфраструктура
| Задача | Приоритет | Статус |
|---|---|---|
| Реестр каналов (map вместо if-цепочек) | перед новыми хендлерами | не сделано |
| Обобщённый EventChannel-помощник | перед EventChannel-плагинами | частично (geolocator) |
| Расширение `build-dart-app.sh` | нужен для Dart-шимов | не сделано |

## 5. Блокирующие решения
1. Архитектура браузера фото: A (Dart-шим, корневой Navigator) vs B (нативный GL-оверлей)
2. Порядок фич: `webview_flutter` или браузер фото первым
3. Делать инфраструктуру до P1/P2 или параллельно?
4. PR #2 (`album-picker`) — мержить план в `master` или оставить открытым
