#pragma once
#include "../plugin_sdk/plugin_sdk.hpp"
#include <cmath>

inline void normalize_vector(vector& v)
{
    float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 0.001f)
    {
        v.x /= len;
        v.y /= len;
        v.z /= len;
    }
}

inline vector normalized(const vector& v)
{
    float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 0.001f)
        return { v.x / len, v.y / len, v.z / len };
    return v;
}
