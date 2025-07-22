#include "../plugin_sdk/plugin_sdk.hpp"
#include "yunara.h"

// Отображаемся в списке плагинов
PLUGIN_NAME("Yunara [Misc]");

// Говорим загрузчику, что это именно misc-плагин
PLUGIN_TYPE(plugin_type::misc);

extern "C" PLUGIN_API bool on_sdk_load(plugin_sdk_core * plugin_sdk_good) {
    // без этого все глобальные указатели (console, state, renderer…) останутся nullptr
    DECLARE_GLOBALS(plugin_sdk_good);

    // регистрируем наши эвенты, GUI и т.п.
    yunara::load();
    return true;
}

extern "C" PLUGIN_API void on_sdk_unload() {
    // чистим то, что навешивали в load()
    yunara::unload();
}
