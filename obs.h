#pragma once
#include <stdbool.h>
#include "types.h"
#include "mongoose.h"

extern struct mg_mgr mgr;

i32 init_conn();


void free_conn();

bool is_scene_exists(const char* scene_name);

i32 create_scene(const char* scene_name);

i32 switch_scene(const char* scene_name);

i32 start_recording();

i32 stop_recording();