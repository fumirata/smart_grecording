#pragma once
#include "log.h"
#include "mongoose.h"
#include "types.h"
#include <stdbool.h>

i32 init_conn();


void free_conn();

i32 is_scene_exists(const char* scene_name, bool *exists);

i32 create_scene(const char* scene_name);

i32 switch_scene(const char* scene_name);

i32 start_record();

i32 stop_record();