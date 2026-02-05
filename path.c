#include "path.h"
#include <stdbool.h>
#include <stdio.h>
#include "log.h"
#include <string.h>

// === Path helpers ===
// Accept both Windows and POSIX separators to handle mixed paths.
bool is_path_separator(char c) {
	return (c == '\\' || c == '/');
}

// Extract the Steam "common/<game>" folder name from a full path.
i32 extract_game_name_from_path(const char* path, char* output, i32 output_size) {
	const char* common_ptr = strstr(path, "common");
	if (common_ptr == NULL) {
		return 1;
	}

	const char* start = common_ptr + 6;
	if (is_path_separator(*start)) {
		start++;
	}

	const char* end = start;
	while (*end != '\0' && !is_path_separator(*end)) {
		end++;
	}

	u64 len = end - start;
	if (len <= 0) {
		return 1;
	}

	strncpy_s(output, output_size, start, len);

	return 0;
}

// Returns 0 on success, non-zero on failure (e.g., output buffer too small).
i32 extract_parent_folder(const char* path, char* output, i32 output_size) {
	u64 len = strlen(path);
	if (len + 1 > (u64)output_size) {
		goto err;
	}

	strcpy_s(output, output_size, path);

	for (u64 i = len; i > 0; --i) {
		if (is_path_separator(output[i])) {
			output[i] = '\0';
			return 0;
		}
	}

err:
	output[0] = '\0';
	return 1;
}
