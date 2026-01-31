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

u8 parse_game_name(const char* path, char* output, u32 output_size) {
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
u8 is_obs_running(bool* running) {
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

u8 launch_obs(const char* game_name) {
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
	char cmd[1024] = "\"";
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

u32 main(u32 argc, char* argv[]) {
	i32 err = 0;
	if (argc < 2) {
		printf("[ERROR]: require at least 1 argument\n");
		err = 1;
		goto err_suspend;
	}

	char game_name[256];
	if (err = parse_game_name(argv[1], game_name, 256)) {
		printf("[ERROR]: failed to parse game name from %s.\n", argv[1]);
		goto err_suspend;
	}


	bool running = false;
	if (is_obs_running(&running)) {
		printf("[ERROR]: failed to loopup OBS process status.\n");
		goto err_suspend;
	}
	if (!running) {
		if (err = launch_obs(game_name)) {
			goto err_suspend;
		}
	}

	init_conn();
	bool exists = is_scene_exists(game_name);
	if (!exists) {
		printf("[WARN]: scene %s does not exists.\n", game_name);
		printf("[WARN]: create scene %s\n", game_name);
		create_scene(game_name);
	}

	switch_scene(game_name);
	start_recording();
	launch_game(argc, argv);
	stop_recording();

err_free_con:
	free_conn();
err_suspend:
	if (err)
		system("pause");

	return err;
}