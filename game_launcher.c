#include "game_launcher.h"
#include <windows.h>
#include <TlHelp32.h>
#include "log.h"
#include "path.h"

bool try_open_child_process(DWORD parent_pid, PROCESS_INFORMATION* child_info);

// Rebuilds the original CLI into a single command line and executes it.
i32 launch_target_game(i32 argc, char* argv[]) {
	i32 err = 0;

	char command_line[2048] = "";
	for (i32 i = 1; i < argc; ++i) {
		strcat_s(command_line, 1024, "\"");
		strcat_s(command_line, 1024, argv[i]);
		strcat_s(command_line, 1024, "\"");
		if (i < argc - 1) {
			strcat_s(command_line, 1024, " ");
		}
	}

	char game_work_dir[2048];
	err = extract_parent_folder(argv[1], game_work_dir, sizeof(game_work_dir));
	if (err) {
		log_warn("could not parse game working directory.");
		return err;
	}
	log_info("parsed game working directory: %s", game_work_dir);

	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	ZeroMemory(&pi, sizeof(pi));
	si.cb = sizeof(si);
	BOOL rc = CreateProcessA(NULL, command_line, NULL, NULL, FALSE, 0, NULL, game_work_dir, &si, &pi);
	err = rc == 0;
	if (!err) {
		CloseHandle(pi.hThread);
		// Wait for the launcher, then follow any child process it spawns (launchers that exit quickly).
		do {
			WaitForSingleObject(pi.hProcess, INFINITE);
			CloseHandle(pi.hProcess);
		} while (try_open_child_process(pi.dwProcessId, &pi));
	}

	return err;
}

bool try_open_child_process(DWORD parent_pid, PROCESS_INFORMATION* child_info) {
	// Snapshot current processes to find a direct child of the launcher.
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return false;
	}

	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	if (!Process32First(snapshot, &entry)) {
		CloseHandle(snapshot);
		return false;
	}

	HANDLE child = NULL;
	do {
		if (entry.th32ParentProcessID == parent_pid) {
			log_info("found child process %ld: %ls", entry.th32ProcessID, entry.szExeFile);
			child = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, entry.th32ProcessID);
			if (child) {
				child_info->hProcess = child;
				child_info->dwProcessId = entry.th32ProcessID;
				break;
			}
		}
	} while (Process32Next(snapshot, &entry));


	CloseHandle(snapshot);
	return child != NULL;
}
