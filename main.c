#include "mongoose.h"
#include "obs.h"
#include "types.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <windows.h>





bool is_separator(char c) {
	return (c == '\\' || c == '/');
}

i32 parse_game_name(const char* path, char* output, u32 output_size) {
	const char* common_ptr = strstr(path, "common");
	if (common_ptr == NULL) {
		return 1;
	}

	const char* start = common_ptr + 6;
	if (is_separator(*start)) {
		start++;
	}

	const char* end = start;
	while (*end != '\0' && !is_separator(*end)) {
		end++;
	}

	u64 len = end - start;
	if (len <= 0) {
		return 1;
	}

	strncpy_s(output, output_size, start, len);

	return 0;
}


// Check if OBS is running.
i32 is_obs_running(bool* running) {
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		return 1;
	}

	PROCESSENTRY32 pe32;
	pe32.dwSize = sizeof(PROCESSENTRY32);

	if (!Process32First(hSnapshot, &pe32)) {
		CloseHandle(hSnapshot);
		return 1;
	}

	bool found = false;
	do {
		if (wcscmp(pe32.szExeFile, L"obs64.exe") == 0) {
			found = true;
			break;
		}
	} while (Process32Next(hSnapshot, &pe32));

	CloseHandle(hSnapshot);
	*running = found;
	return 0;
}

i32 launch_obs(const char* game_name) {
	bool running = false;
	u32 err = is_obs_running(&running);
	if (err) {
		printf("ERROR: failed to check whether OBS is running.\n");
		return 1;
	}
	if (running)
		return 0;

	char* working_dir = "C:\\Program Files\\obs-studio\\bin\\64bit";
	char exe_path[128];
	strcpy_s(exe_path, 128, working_dir);
	strcat_s(exe_path, 128, "\\obs64.exe");

	HINSTANCE result = ShellExecuteA(NULL, "open", exe_path, "--minimize-to-tray", working_dir, SW_SHOWNORMAL);
	if ((intptr_t)result > 32) {
		printf("OBS Launched successfully.\n");
	} else {
		printf("Failed to launch OBS. Error code: %lld\n", (intptr_t)result);
		return 1;
	}

	Sleep(10000);	// Wait for OBS to fully started.
	return 0;
}





void launch_game(u32 argc, char* argv[]) {
	char cmd[2048] = "\"";
	for (u32 i = 1; i < argc; ++i) {
		strcat_s(cmd, 1024, "\"");
		strcat_s(cmd, 1024, argv[i]);
		strcat_s(cmd, 1024, "\"");
		if (i < argc - 1) {
			strcat_s(cmd, 1024, " ");
		}
	}
	strcat_s(cmd, 1024, "\"");
	printf("# %s\n", cmd);
	system(cmd);
}

void print_args(u32 argc, char* argv[]) {
	for (i32 i = 1; i < argc; ++i) {
		log_info("arg[%d]: %s", i, argv[i]);
	}
}

u32 main(u32 argc, char* argv[]) {
	print_args(argc, argv);

	i32 err = 0;
	if (argc < 2) {
		log_fatal("require at least 1 argument");
		err = 1;
		goto err_suspend;
	}

	char game_name[256];
	if (err = parse_game_name(argv[1], game_name, sizeof(game_name))) {
		log_fatal("failed to parse game name from %s", argv[1]);
		goto err_suspend;
	}
	log_info("parsed game name: %s", game_name);


	/*bool running = false;
	if (is_obs_running(&running)) {
		printf("[ERROR]: failed to loopup OBS process status.\n");
		goto err_suspend;
	}
	if (!running) {
		if (err = launch_obs(game_name)) {
			goto err_suspend;
		}
	}*/

	init_conn();
	bool exists;
	if (err = is_scene_exists(game_name, &exists)) {
		log_fatal("failed to check whether scene exists");
		goto err_free_con;
	}
	if (!exists) {
		log_warn("scene %s does not exists", game_name);
		log_warn("create scene %s", game_name);
		if (err = create_scene(game_name)) {
			log_fatal("failed to create scene");
			goto err_free_con;
		}
	}

	if (err = switch_scene(game_name)) {
		log_fatal("failed to switch to scene %s", game_name);
		goto err_free_con;
	}

	if (err = start_record()) {
		log_fatal("failed to start record");
		goto err_free_con;
	}

	launch_game(argc, argv);

	if (err = stop_record()) {
		log_fatal("failed to stop record. Just stop it manually");
		goto err_free_con;
	}

err_free_con:
	free_conn();
err_suspend:
	if (err)
		system("pause");

	return err;
}