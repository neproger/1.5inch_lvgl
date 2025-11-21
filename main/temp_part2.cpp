
    if (s_ui_mode == UiMode::Screensaver)
    {
        s_ui_mode = UiMode::Rooms;
        if (!s_room_pages.empty())
        {
            int rooms = static_cast<int>(s_room_pages.size());
            if (s_current_room_index < 0 || s_current_room_index >= rooms)
            {
                s_current_room_index = 0;
            }
            lv_disp_load_scr(s_room_pages[s_current_room_index].root);
        }
        return;
    }

    const int table_sz = sizeof(button_event_table) / sizeof(button_event_table[0]);
    const char *label = (ev >= 0 && ev < table_sz) ? button_event_table[ev] : "BUTTON_UNKNOWN";
    switch (ev)
    {
    case 4:
        ESP_LOGI(TAG_UI, "%s", label);
        handle_single_click();
        break;
    default:
        break;
    }
}

static void handle_single_click(void)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_toggle_us < 700 * 1000)
    {
        return;
    }
    if (s_ha_req_task != NULL)
    {
        ESP_LOGW(TAG_UI, "Toggle request already in progress");
        return;
    }
    if (!router::is_connected())
    {
        ESP_LOGW(TAG_UI, "No MQTT connection for toggle");
        return;
    }

    std::string entity_id;

    if (!s_room_pages.empty() &&
        s_current_room_index >= 0 &&
        s_current_room_index < static_cast<int>(s_room_pages.size()))
    {
        const RoomPage &page = s_room_pages[s_current_room_index];
        if (!page.devices.empty() &&
            s_current_device_index >= 0 &&
            s_current_device_index < static_cast<int>(page.devices.size()))
        {
            entity_id = page.devices[static_cast<size_t>(s_current_device_index)].entity_id;
        }
    }

    if (entity_id.empty())
    {
        ESP_LOGW(TAG_UI, "No entity selected for toggle");
        return;
    }

    trigger_toggle_for_entity(entity_id);
}

static void trigger_toggle_for_entity(const std::string &entity_id)
{
    if (entity_id.empty())
    {
        ESP_LOGW(TAG_UI, "trigger_toggle_for_entity: empty entity_id");
        return;
    }

    int64_t now = esp_timer_get_time();
    if (now - s_last_toggle_us < 700 * 1000)
    {
        return;
    }
    if (!router::is_connected())
    {
        ESP_LOGW(TAG_UI, "No MQTT connection for toggle");
        return;
    }
    if (s_ha_req_task != NULL)
    {
        ESP_LOGW(TAG_UI, "Toggle already in progress");
        return;
    }

    s_pending_toggle_entity = entity_id;
    ui_set_switches_enabled(false);
    ui_show_spinner();

    if (s_ha_req_task == NULL)
    {
        char *arg = static_cast<char *>(std::malloc(entity_id.size() + 1));
        if (!arg)
        {
            ESP_LOGE(TAG_UI, "No memory for toggle arg");
            s_pending_toggle_entity.clear();
            ui_set_switches_enabled(true);
            ui_hide_spinner();
            return;
        }
        std::memcpy(arg, entity_id.c_str(), entity_id.size() + 1);

        BaseType_t ok = xTaskCreate(ha_toggle_task, "ha_toggle", 4096, arg, 4, &s_ha_req_task);
        if (ok != pdPASS)
        {
            std::free(arg);
            s_ha_req_task = NULL;
            ESP_LOGW(TAG_UI, "Failed to create ha_toggle task");
            ui_set_switches_enabled(true);
            s_pending_toggle_entity.clear();
            ui_hide_spinner();
        }
    }
}

static void ha_toggle_task(void *arg)
{
    char *entity = static_cast<char *>(arg);

    s_last_toggle_us = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(200));

    if (!wifi_manager_is_connected())
    {
        ESP_LOGW(TAG_UI, "No WiFi, cannot toggle entity");
        if (entity)
            std::free(entity);
        s_pending_toggle_entity.clear();
        ui_set_switches_enabled(true);
        ui_hide_spinner();
        s_ha_req_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = router::toggle(entity);

    if (entity)
        std::free(entity);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG_UI, "MQTT toggle error: %d", (int)err);
    }

    s_ha_req_task = NULL;
    s_pending_toggle_entity.clear();
    ui_set_switches_enabled(true);
    ui_hide_spinner();
    vTaskDelete(NULL);
}

static void switch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_VALUE_CHANGED)
    {
        return;
    }

    lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (lv_obj_has_state(sw, LV_STATE_DISABLED))
    {
        ESP_LOGI(TAG_UI, "Toggle ignored, control disabled");
        return;
    }
    if (s_ha_req_task != NULL)
    {
        ESP_LOGI(TAG_UI, "Toggle in progress, ignoring switch");
        return;
    }
    std::string entity_id;

    for (const auto &page : s_room_pages)
    {
        for (const auto &w : page.devices)
        {
            if (w.control == sw)
            {
                entity_id = w.entity_id;
                break;
            }
        }
        if (!entity_id.empty())
            break;
    }

    if (entity_id.empty())
    {
        ESP_LOGW(TAG_UI, "switch_event_cb: control without entity");
        return;
    }

    trigger_toggle_for_entity(entity_id);
}

static void ui_show_spinner(void)
{
    lvgl_port_lock(-1);
    if (s_spinner)
    {
        lv_obj_del(s_spinner);
        s_spinner = NULL;
    }
    lv_obj_t *scr = lv_screen_active();
    s_spinner = lv_spinner_create(scr);
    lv_obj_set_size(s_spinner, 24, 24);
    lv_obj_align(s_spinner, LV_ALIGN_BOTTOM_MID, 0, -10);
    lvgl_port_unlock();
}

static void ui_hide_spinner(void)
{
    lvgl_port_lock(-1);
    if (s_spinner)
    {
        lv_obj_del(s_spinner);
        s_spinner = NULL;
    }
    lvgl_port_unlock();
}

static void ui_set_switches_enabled(bool enabled)
{
    lvgl_port_lock(-1);
    for (auto &page : s_room_pages)
    {
        for (auto &w : page.devices)
        {
            if (!w.control)
                continue;
            if (enabled)
            {
                lv_obj_clear_state(w.control, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(w.control, LV_STATE_DISABLED);
            }
        }
    }
    lvgl_port_unlock();
}

static void ui_show_splash_screen(void)
{
    if (s_splash_root)
    {
        lv_disp_load_scr(s_splash_root);
        return;
