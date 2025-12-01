#include "locale_ru.hpp"

namespace ui
{
    namespace locale_ru
    {

        const char *kWeekdayNames[7] = {
            "Понедельник",
            "Вторник",
            "Среда",
            "Четверг",
            "Пятница",
            "Суббота",
            "Воскресенье"};

        const char *kMonthNames[13] = {
            "",
            "Январь",
            "Февраль",
            "Март",
            "Апрель",
            "Май",
            "Июнь",
            "Июль",
            "Август",
            "Сентябрь",
            "Октябрь",
            "Ноябрь",
            "Декабрь"};

        const char *weather_condition_to_text(const std::string &cond)
        {
            if (cond == "clear")
                return "Ясно";
            if (cond == "clear-night")
                return "Ясно (ночь)";
            if (cond == "sunny")
                return "Солнечно";
            if (cond == "partlycloudy")
                return "Переменная облачность";
            if (cond == "cloudy")
                return "Облачно";
            if (cond == "overcast")
                return "Пасмурно";
            if (cond == "rainy")
                return "Дождь";
            if (cond == "pouring")
                return "Ливень";
            if (cond == "lightning")
                return "Гроза";
            if (cond == "lightning-rainy")
                return "Гроза с дождём";
            if (cond == "snowy")
                return "Снег";
            if (cond == "snowy-rainy")
                return "Снег с дождём";
            if (cond == "hail")
                return "Град";
            if (cond == "fog")
                return "Туман";
            if (cond == "windy")
                return "Ветрено";
            if (cond == "windy-variant")
                return "Ветрено, переменная облачность";
            if (cond == "exceptional")
                return "Необычная погода";
            return cond.c_str();
        }

    } // namespace locale_ru
} // namespace ui

