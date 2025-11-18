Сделал черновую реализацию скринсейвера, завязанную на LVGL‑таймер простоя, и вывел погоду именно туда.

Что теперь происходит

В ui_app.cpp заведён режим UI:
enum class UiMode { Rooms, Screensaver };
static UiMode s_ui_mode = UiMode::Rooms;
Добавлены объекты скринсейвера:
static lv_obj_t *s_screensaver_root = NULL;
static lv_obj_t *s_weather_label = NULL;
static lv_timer_t *s_idle_timer = NULL;
static const uint32_t kScreensaverTimeoutMs = 5000; (5 секунд простоя).
Инициализация скринсейвера

В ui_app.cpp реализована функция:
extern "C" void ui_init_screensaver_support(void):
вызывает ui_build_screensaver();
один раз создаёт s_idle_timer = lv_timer_create(idle_timer_cb, 500, NULL);
сразу дергает ui_update_weather_label(); чтобы подтянуть текущую погоду.
В main/main.cpp:
внутри lvgl_port_lock(0) ... lvgl_port_unlock(); теперь:
lvgl_port_lock(0);
ui_app_init();
ui_init_screensaver_support();
lvgl_port_unlock();
так мы гарантируем, что создание скринсейвера и таймера происходит под мьютексом LVGL.
Экран скринсейвера

static void ui_build_screensaver(void) создаёт отдельный экран:
чёрный root (s_screensaver_root) размером LV_HOR_RES x LV_VER_RES;
на нём один большой лейбл s_weather_label по центру (Montserrat_40) с текстом погоды.
Погода в комнатах больше не рисуется:
из ui_build_room_pages() убран page.weather_label и связанная верстка.
В RoomPage указатель weather_label оставлен, но он нигде не создаётся, так что на обычных страницах погода не отображается.
Таймер простоя LVGL

static void idle_timer_cb(lv_timer_t *timer):
берёт lv_display_t *disp = lv_display_get_default();
считает uint32_t inactive_ms = lv_display_get_inactive_time(disp);
если s_ui_mode == UiMode::Rooms и inactive_ms >= kScreensaverTimeoutMs:
делает lv_disp_load_scr(s_screensaver_root);
s_ui_mode = UiMode::Screensaver;.
Таймер создаётся с периодом 500 мс, так что переход на скринсейвер будет примерно через 5–5.5 секунд без активности.
Выход из скринсейвера

В обработчиках ввода мы теперь:
В LVGL_knob_event и LVGL_button_event:
сразу вызываем lv_display_trigger_activity(NULL); чтобы LVGL сбрасывал last_activity_time.
если сейчас s_ui_mode == UiMode::Screensaver:
переключаемся назад на текущую комнату:
s_ui_mode = UiMode::Rooms;
if (!s_room_pages.empty()) {
    // нормализуем индекс
    ...
    lv_disp_load_scr(s_room_pages[s_current_room_index].root);
}
return; // этот поворот/клик только будит экран
То есть первый поворот/клик просто снимает скринсейвер, не листает комнаты и не включает устройства — как и обсуждали.
Погода на скринсейвере

weather_task остался как раньше:
раз в 5 секунд делает HTTP‑запрос к /api/template,
парсит CSV (Temperature,Condition),
обновляет стейт через state::set_weather(temp_c, cond);
под lvgl_port_lock(-1) вызывает ui_update_weather_label();.
ui_update_weather_label() теперь:
формирует строку вида "13.5°C, clear-night" (или "--°C, --" если нет данных);
всё ещё пробегается по RoomPage::weather_label (но там везде nullptr — по факту ничего не рисуется в комнатах);
главное — если s_weather_label не nullptr, обновляет текст на экране скринсейвера:
if (s_weather_label) {
    lv_label_set_text(s_weather_label, buf);
}
Итоговое поведение

После загрузки устройства:
обычные “room pages” работают как раньше;
погода на них не отображается.
Через ~5 секунд без тача/крутилки/кнопки:
дисплей переключается на чёрный экран скринсейвера с крупной строкой температура + состояние.
Первый поворот энкодера или клик кнопки:
просто возвращает последний экран комнаты;
последующие действия уже листают комнаты / делают toggle.
Погода обновляется раз в 5 секунд через HTTP и всегда отображается на скринсейвере.
Если хочешь, дальше можно:

добавить на скринсейвер время/дату;
сделать лёгкое затемнение/анимацию при входе/выходе;
научить скринсейвер просыпаться и от тача (через LVGL‑события на s_screensaver_root).
3 files changed
+121
-17

Undo

View all changes
main.cpp
+1
-0

ui_app.cpp
+117
-17

ui_app.h
+3
-0

