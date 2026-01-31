#pragma once

// === Includes ===
#include "log.h"
#include "mongoose.h"
#include "types.h"
#include <stdbool.h>

// === Connection lifecycle ===
#ifndef OBS_CONNECT_TIMEOUT_MS
#define OBS_CONNECT_TIMEOUT_MS 3000
#endif

#ifndef OBS_REQUEST_TIMEOUT_MS
#define OBS_REQUEST_TIMEOUT_MS 5000
#endif

i32 obs_connect();

void obs_disconnect();

// === Scene operations ===
i32 obs_scene_exists(const char* scene_name, bool *exists);

i32 obs_create_scene(const char* scene_name);

i32 obs_set_current_scene(const char* scene_name);

// === Recording operations ===
i32 obs_start_recording();

i32 obs_stop_recording();
