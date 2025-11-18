# План архитектуры state_manager / UI / MQTT

## 1. Модель данных

Сущности Home Assistant отражаем в памяти так:

- **Area**
  - `std::string id;` — `AREA_ID`
  - `std::string name;` — `AREA_NAME`

- **Entity**
  - `std::string id;` — `ENTITY_ID` (например, `switch.wifi_breaker_t_switch_1`)
  - `std::string name;` — `ENTITY_NAME` (например, `Relay_01`)
  - `std::string state;` — `STATE` (`"on"` / `"off"` / и т.п.)
  - `std::string area_id;` — ссылка на `Area` (`AREA_ID`)

Хранилище:

- `std::vector<Area>` — список помещений (areas).
- `std::vector<Entity>` — список сущностей.

Индексы:

- `std::unordered_map<std::string, size_t> entity_index_by_id;`
- `std::unordered_map<std::string, size_t> area_index_by_id;`

---

## 2. Модуль `state_manager`

Файлы:

- `main/state_manager.hpp`
- `main/state_manager.cpp`

Публичный C++ API (`namespace state`):

### 2.1. Инициализация и изменение состояния

- `bool init_from_csv(const char *csv, size_t len);`  
  Разбор bootstrap‑CSV и первичное заполнение списков областей и сущностей.

- `bool set_entity_state(const std::string &entity_id, const std::string &state);`  
  Обновляет состояние сущности по данным, пришедшим от MQTT/`router`.  
  Напрямую из UI эта функция **не вызывается** — UI лишь инициирует действие, а реальное состояние приходит от HA.

### 2.2. Доступ к данным

- `const std::vector<Area> &areas();`
- `const std::vector<Entity> &entities();`
- `const Entity *find_entity(const std::string &id);`

### 2.3. Подписки UI

- Тип слушателя:
  - `using EntityListener = std::function<void(const Entity &)>;`

- Операции:
  - `int subscribe_entity(const std::string &id, EntityListener cb);`
  - `void unsubscribe(int subscription_id);`

При вызове `set_entity_state` `state_manager` уведомляет все подписки, соответствующие `entity_id`.

---

## 3. Bootstrap через CSV

Формат CSV:

- Первая строка — заголовок:
  - `AREA_ID,AREA_NAME,ENTITY_ID,ENTITY_NAME,STATE`
- Остальные строки — данные (каждая строка описывает одну сущность и её текущее состояние).

Алгоритм:

1. В `perform_bootstrap_request()` выполняется HTTP‑запрос, ответ читается в `buf`.
2. В `state_manager`:
   - `split_lines` — разбиение на строки по `\n`.
   - парсер одной строки (`split_csv_line`):
     - разделяет по `,` на 5 полей;
     - обрезает пробелы.
3. `init_from_csv`:
   - очищает прежнее состояние (areas/entities/индексы);
   - ищет строку с заголовком, проверяет, что он совпадает (без учёта регистра);
   - для каждой строки‑данных:
     - извлекает `area_id`, `area_name`, `entity_id`, `entity_name`, `state`;
     - если `area_id` ещё не встречался — добавляет `Area` и индексирует её;
     - добавляет `Entity` и индексирует по `id` (дубликаты игнорируются);
   - при ошибках строк — пишет лог и пропускает строку.

Использование:

- В `perform_bootstrap_request()` после успешного HTTP‑ответа (2xx):
  - вызываем `state::init_from_csv(buf, strlen(buf));`

---

## 4. Связка с UI (`ui_app.cpp`)

Идея: UI не хранит своё отдельное состояние сущностей, а опирается на `state_manager` как на единственный источник правды.

1. В `ui_app_init()`:
   - запрашиваем список сущностей через `state::entities()`;
   - при необходимости фильтруем по шаблону (например, `switch.*`);
   - для каждой отображаемой сущности:
     - создаём UI‑элемент (кнопка/тайл);
     - подписываемся через `state::subscribe_entity(entity.id, ...)` и обновляем виджет при изменении состояния.

2. На пользовательское действие (клик/нажатие):
   - UI **не** меняет состояние сущности напрямую;
   - UI вызывает MQTT‑действие через `router` (например, `router::toggle(entity_id)`), инициируя изменение на стороне HA;
   - HA применяет действие и публикует новое состояние по MQTT;
   - `router` получает это состояние и через `state::set_entity_state(...)` обновляет `state_manager`;
   - `state_manager` оповещает подписчиков, UI получает обновлённые данные и перерисовывается.

Таким образом, UI всегда отображает то состояние, которое пришло от HA, а не локальную “догадку”.

---

## 5. Связка с MQTT / `router`

В `router`/`ha_mqtt` обрабатываются сообщения вида `ha/state/<entity_id>`.

1. При получении MQTT‑сообщения:
   - проверяем префикс `ha/state/`;
   - извлекаем `entity_id` из хвоста топика;
   - интерпретируем payload как `state` (`"on"`, `"off"`, и т.п.).

2. Обновляем state_manager:
   - вызываем `state::set_entity_state(entity_id, state);`

В результате:

- обновляется внутренний `state_manager`;
- все подписанные через `state::subscribe_entity` обработчики вызываются с новым состоянием;
- UI реагирует и обновляет индикацию.

Отдельно реализуется команда toggle:

- UI вызывает `router::toggle(entity_id)`;
- `router` публикует MQTT‑команду (например, в топик `HA_MQTT_CMD_TOGGLE_TOPIC`);
- после обработки HA присылает новое состояние в `ha/state/<entity_id>`, что обновляет state_manager как описано выше.

---

## 6. Потокобезопасность

Места вызова `state_manager`:

- bootstrap (`init_from_csv`) — в `app_main` (main task);
- UI — в `ui_app` / LVGL‑таске;
- MQTT — в MQTT‑таске через `router`.

Требования:

- доступ к структурам `g_areas`, `g_entities`, индексам и списку слушателей должен быть защищён (мьютекс или критические секции FreeRTOS);
- все функции публичного API (`init_from_csv`, `set_entity_state`, `subscribe_entity`, `unsubscribe`, `entities`, `areas`, `find_entity`) должны быть безопасны при вызове из разных задач;
- в `set_entity_state` важно:
  - в одной критической секции обновлять `Entity.state`;
  - затем вызывать подписчиков либо под той же защитой, либо с аккуратным копированием данных, чтобы избежать гонок.

---

## 7. Интеграция с остальными модулями

- `main/main.cpp`:
  - выделяет буфер `buf` и вызывает `perform_bootstrap_request()` (например, 2048–4096 байт под CSV);
  - после успешного HTTP‑ответа (`Bootstrap HTTP ok`) вызывает `state::init_from_csv(...)` и логирует количество областей и сущностей.

- `ui_app.cpp`:
  - получает список ID и имён сущностей из `state::entities()`;
  - строит UI на основе этого списка;
  - подписывается на изменения состояний через `state::subscribe_entity`.

- `router.cpp` / `ha_mqtt`:
  - на старте подписывается на `ha/state/<entity_id>` для всех сущностей;
  - при получении MQTT‑сообщений вызывает `state::set_entity_state(...)`;
  - предоставляет UI API для инициирования действий (`router::toggle` и т.п.).

---

## 8. Логирование и пример сценария

Полезные логи:

- при инициализации:
  - `[state] parsed N areas, M entities`
- при изменении состояния:
  - `[state] entity <id>: <old> -> <new>`

Типовой сценарий:

1. После bootstrap UI отображает состояние сущностей из CSV (например, некоторые `off`).
2. Пользователь нажимает кнопку в UI:
   - UI вызывает `router::toggle(entity_id)`, что публикует MQTT‑команду.
3. HA исполняет команду и отправляет новое состояние в `ha/state/<entity_id>`.
4. `router` получает сообщение и вызывает `state::set_entity_state(entity_id, state)`.
5. `state_manager` уведомляет подписчиков, UI получает новое состояние и обновляет отображение.

---

## 9. Роли модулей (итог)

- **state_manager**  
  Единственный источник правды по областям, сущностям и их состоянию.  
  Инициализируется из CSV, обновляется по MQTT через `router`, оповещает UI.

- **UI (`ui_app`)**  
  Только:
  - инициирует действия пользователя (toggle и т.п.) через `router`;
  - отображает состояние, подписываясь на `state_manager`.  
  Не меняет состояние напрямую.

- **`router` / `ha_mqtt`**  
  Мост между HA и `state_manager`:
  - отправляет команды (toggle и т.д.) по MQTT;
  - принимает новые состояния от HA;
  - транслирует их в `state_manager` через `set_entity_state`.

