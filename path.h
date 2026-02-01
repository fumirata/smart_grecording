#pragma once
#include "types.h"

i32 extract_game_name_from_path(const char* path, char* output, i32 output_size);

void extract_parent_folder(const char* path, char* output);