#include "../plugin_sdk/plugin_sdk.hpp"
#include "yunara.h"


PLUGIN_NAME("Yunara");


PLUGIN_TYPE(plugin_type::misc);

extern "C" PLUGIN_API bool on_sdk_load(plugin_sdk_core* plugin_sdk_good) {
   
    DECLARE_GLOBALS(plugin_sdk_good);

  
    yunara::load();
    return true;
}

extern "C" PLUGIN_API void on_sdk_unload() {
    
    yunara::unload();
}
