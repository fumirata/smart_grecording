// === Includes ===
#include "game_launcher.h"
#include "mongoose.h"
#include "obs.h"
#include "types.h"
#include <windows.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <wchar.h>
#include "path.h"






// === OBS helpers ===
// Check for an existing OBS process by executable name.
i32 is_obs_process_running(bool* is_running) {
	HANDLE process_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (process_snapshot == INVALID_HANDLE_VALUE) {
		return 1;
	}

	PROCESSENTRY32 process_entry;
	process_entry.dwSize = sizeof(PROCESSENTRY32);

	if (!Process32First(process_snapshot, &process_entry)) {
		CloseHandle(process_snapshot);
		return 1;
	}

	bool obs_found = false;
	do {
		if (wcscmp(process_entry.szExeFile, L"obs64.exe") == 0) {
			obs_found = true;
			break;
		}
	} while (Process32Next(process_snapshot, &process_entry));

	CloseHandle(process_snapshot);
	*is_running = obs_found;
	return 0;
}

// Launch OBS if it is not already running.
i32 launch_obs(const char* game_name) {
	(void)game_name; // Supress unused reference warning
	bool is_running = false;
	i32 err = is_obs_process_running(&is_running);
	if (err) {
		printf("ERROR: could not determine whether OBS is running.\n");
		return 1;
	}
	if (is_running)
		return 0;

	char* obs_working_dir = "C:\\Program Files\\obs-studio\\bin\\64bit";
	char obs_exe_path[128];
	strcpy_s(obs_exe_path, 128, obs_working_dir);
	strcat_s(obs_exe_path, 128, "\\obs64.exe");

	HINSTANCE result = ShellExecuteA(NULL, "open", obs_exe_path, "--minimize-to-tray", obs_working_dir, SW_SHOWNORMAL);
	if ((intptr_t)result > 32) {
		printf("OBS launched successfully.\n");
	} else {
		printf("ERROR: failed to launch OBS (code: %Id).\n", (intptr_t)result);
		return 1;
	}

	Sleep(10000);	// Wait for OBS to fully started.
	return 0;
}

// === App helpers ===
// Rebuilds the original CLI into a single command line and executes it.
//void launch_target_game(i32 argc, char* argv[]) {
//	char command_line[2048] = "\"";
//	for (i32 i = 1; i < argc; ++i) {
//		strcat_s(command_line, 1024, "\"");
//		strcat_s(command_line, 1024, argv[i]);
//		strcat_s(command_line, 1024, "\"");
//		if (i < argc - 1) {
//			strcat_s(command_line, 1024, " ");
//		}
//	}
//	strcat_s(command_line, 1024, "\"");
//	printf("# %s\n", command_line);
//	system(command_line);
//}

// Log CLI arguments for debugging.
void log_cli_args(i32 argc, char* argv[]) {
	for (i32 i = 1; i < argc; ++i) {
		log_info("arg[%d]: %s", i, argv[i]);
	}
}

// === Entry point ===
i32 main(i32 argc, char* argv[]) {
	log_cli_args(argc, argv);

	i32 err = 0;
	if (argc < 2) {
		log_fatal("expected at least 1 argument (path to game executable).");
		err = 1;
		goto err_suspend;
	}

	char target_scene_name[256];
	err = extract_game_name_from_path(argv[1], target_scene_name, sizeof(target_scene_name));
	if (err) {
		log_fatal("could not parse game name from path: %s", argv[1]);
		goto err_suspend;
	}
	log_info("target scene name: %s", target_scene_name);


	/*bool running = false;
	if (is_obs_running(&running)) {
		printf("[ERROR]: failed to loopup OBS process err.\n");
		goto err_suspend;
	}
	if (!running) {
		if (err = launch_obs(game_name)) {
			goto err_suspend;
		}
	}*/

	err = obs_connect();
	if (err) {
		log_fatal("could not connect to OBS");
		goto err_suspend;
	}

	bool exists;
	err = obs_scene_exists(target_scene_name, &exists);
	if (err) {
		log_fatal("could not check whether the scene exists");
		goto err_free_con;
	}
	if (!exists) {
		log_warn("scene '%s' does not exist", target_scene_name);
		log_warn("creating scene '%s'", target_scene_name);
		err = obs_create_scene(target_scene_name);
		if (err) {
			log_fatal("could not create scene");
			goto err_free_con;
		}
	}

	err = obs_set_current_scene(target_scene_name);
	if (err) {
		log_fatal("could not switch to scene '%s'", target_scene_name);
		goto err_free_con;
	}

	err = obs_start_recording();
	if (err) {
		log_fatal("could not start recording");
		goto err_free_con;
	}

	err = launch_target_game(argc, argv);
	if (err) {
		log_fatal("could not start game");
		goto err_free_con;
	}

	err = obs_stop_recording();
	if (err) {
		log_fatal("could not stop recording; please stop it manually");
		goto err_free_con;
	}

err_free_con:
	obs_disconnect();
err_suspend:
	if (err)
		system("pause");

	return err;
}
