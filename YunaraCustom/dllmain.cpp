#include "../plugin_sdk/plugin_sdk.hpp"
#include "yunara.h"

PLUGIN_NAME("Yunara Misc")
PLUGIN_TYPE(plugin_type::misc)

extern "C" __declspec(dllexport) bool on_sdk_load(plugin_sdk_core * plugin_sdk_good)
{
    plugin_sdk = plugin_sdk_good;
    yunara::load();
    return true;
}

extern "C" __declspec(dllexport) void on_sdk_unload()
{
    yunara::unload();
}