# 1. Модель данных

Описываем сущности:

- **Area**
  - `std::string id;` — `AREA_ID`
  - `std::string name;` — `AREA_NAME`

- **Entity**
  - `std::string id;` — `ENTITY_ID` (например, `switch.wifi_breaker_t_switch_1`)
  - `std::string name;` — `ENTITY_NAME` (например, `Relay_01`)
  - `std::string state;` — `STATE` (`on`/`off`/…)
  - `std::string area_id;` — ссылка на `Area` (`AREA_ID`)

Структура состояния:

- `std::vector<Area>` — список уникальных пространств (areas).
- `std::vector<Entity>` — плоский список всех сущностей.

Ускоряющие индексы:

- `std::unordered_map<std::string, size_t> entity_index_by_id;`
- `std::unordered_map<std::string, size_t> area_index_by_id;`

---

# 2. Новый модуль state‑менеджера

Добавляем файлы:

- `main/state_manager.hpp`
- `main/state_manager.cpp`

Публичный C++ API (`namespace state`):

## 2.1. Инициализация и обновление

- `void init_from_csv(const char *csv, size_t len);`  
  Парсит ответ bootstrap (CSV) и строит внутреннее состояние.

- `bool set_entity_state(const std::string &entity_id, const std::string &state);`  
  Обновляет состояние сущности (по MQTT или локальному действию UI).

## 2.2. Чтение

- `const std::vector<Area> &areas();`
- `const std::vector<Entity> &entities();`
- `const Entity *find_entity(const std::string &id);`

## 2.3. Подписки UI

- Тип колбэка:
  - `using EntityListener = std::function<void(const Entity &)>;`

- Регистрация:
  - `int subscribe_entity(const std::string &id, EntityListener cb);`
  - `void unsubscribe(int subscription_id);`

При каждом `set_entity_state` state‑менеджер оповещает подписчиков.

---

# 3. Парсинг CSV ответа bootstrap

Формат CSV:

- Первая строка — заголовок:
  - `AREA_ID,AREA_NAME,ENTITY_ID,ENTITY_NAME,STATE`
- Далее — строки данных (возможен пустой ряд между заголовком и первой записью).

Логика парсера:

1. Получаем `buf` из `http_send` (в `perform_bootstrap_request()`).
2. В state‑менеджере реализуем:
   - `split_lines` — разбивка по `
`, игнор пустых строк.
   - `parse_csv_line`:
     - Разбивает строку по `,` на 5 полей.
     - Тримит пробелы и ``.
     - Не обрабатывает кавычки с запятыми внутри (не нужны для наших данных).
3. Алгоритм `init_from_csv`:
   - Проверить заголовок (первые поля), иначе залогировать и вернуть `false`.
   - Очистить `areas`, `entities` и индексы.
   - Для каждой строки данных:
     - Прочитать `area_id`, `area_name`, `entity_id`, `entity_name`, `state`.
     - Если `area_id` ещё не встречался — добавить в `areas` и `area_index_by_id`.
     - Добавить `Entity` в `entities` и записать индекс в `entity_index_by_id`.
   - Если строка не даёт 5 полей — лог и пропуск.

Где вызывается:

- В `perform_bootstrap_request()` после успешного HTTP‑ответа (2xx):
  - `state::init_from_csv(buf, strlen(buf));`

---

# 4. Связка с UI (`ui_app.cpp`)

Сейчас UI жёстко знает `entity[0].id`, `entity[1].id` и т.д. План:

1. В `ui_app_init()`:
   - Получать список сущностей через `state::entities()`.
   - Фильтровать нужные типы (например, только `switch.*`).
   - Для каждой сущности:
     - Создавать виджет (кнопка/тумблер).
     - Подписываться через `state::subscribe_entity(entity.id, ...)` и обновлять виджет при изменении.

2. При действии пользователя (нажатие на кнопку):
   - Вызывать `state::set_entity_state(entity_id, new_state);` для локального обновления.
   - Отправлять MQTT‑команду через текущий `ha_mqtt`/`router`.

Таким образом UI не хранит свой отдельный список устройств, а опирается на state‑менеджер.

---

# 5. Связка с MQTT / `router`

В `router`/`ha_mqtt` уже есть подписка на `ha/state/switch...`. Нужно:

1. В обработчике входящих сообщений:
   - Вытащить `entity_id` из топика (последняя часть или заранее согласованный шаблон).
   - Преобразовать payload в строку `state` (`"on"`, `"off"`, и т.д.).

2. Вызвать:
   - `state::set_entity_state(entity_id, state);`

Это:

- Обновит внутренний state.
- Оповестит все подписанные UI‑колбэки.

---

# 6. Потокобезопасность (thread‑safety)

Где вызывается state‑менеджер:

- Bootstrap (`init_from_csv`) — в `app_main` (main task).
- Подписки UI — в `ui_app_init()` (под LVGL‑мьютексом).
- Обновления из MQTT — в MQTT‑таске.

Чтобы избежать гонок:

- Внутри state‑менеджера держать один мьютекс (FreeRTOS или обёртка).
- Все функции, которые читают/меняют состояние (`init_from_csv`, `set_entity_state`, `subscribe_entity`, `entities`, `areas`) работают под мьютексом.
- В `set_entity_state`:
  - Под мьютексом обновить состояние + скопировать список слушателей для данного entity.
  - Отпустить мьютекс.
  - Вызвать колбэки уже без мьютекса.

---

# 7. Минимальные изменения в существующем коде

- `main/main.cpp`:
  - Увеличить размер буфера `buf` в `perform_bootstrap_request()` (например, до 2048–4096 байт), чтобы влез весь CSV.
  - После успешного HTTP и до `Bootstrap HTTP ok` вызвать `state::init_from_csv(...)` и залогировать количество entities/areas.

- `ui_app.cpp`:
  - Убрать жёстко зашитые ID сущностей.
  - Перейти на динамический список из `state::entities()` и подписки.

- `router.cpp` / `ha_mqtt`:
  - В местах, где сейчас просто логируются новые MQTT‑состояния, дополнительно вызывать `state::set_entity_state(...)`.

---

# 8. Отладка и проверки

Полезные логи:

- При инициализации:
  - `[state] parsed N areas, M entities`
- При обновлении по MQTT:
  - `[state] entity switch.wifi_breaker_t_switch_1: off -> on`

Ручной сценарий проверки:

1. Старт устройства → UI показывает состояния из CSV (сейчас всё `off` по тестовому endpoint’у).
2. Нажать кнопку в UI → отправка MQTT publish → брокер отвечает новым состоянием → MQTT‑слой вызывает `set_entity_state` → UI обновляется через слушателя.
