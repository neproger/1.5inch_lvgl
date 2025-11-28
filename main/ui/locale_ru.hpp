#pragma once

#include <string>

namespace ui
{
    namespace locale_ru
    {

        extern const char *kWeekdayNames[7];
        extern const char *kMonthNames[13];

        const char *weather_condition_to_text(const std::string &cond);

    } // namespace locale_ru
} // namespace ui

