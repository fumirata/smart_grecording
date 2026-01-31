#include "obs.h"
#include <string.h> // Ensure string.h is included for strncmp, strlen

static const char* url = "ws://127.0.0.1:4455";

typedef struct ObsCtx {
	bool identified;
	bool task_complete;
	struct mg_connection* con;
	char* data;
	u32 data_len;
} ObsCtx;

struct mg_mgr mgr;
ObsCtx ctx = { false, false, NULL, NULL, 0 };

void respond_hello(struct mg_connection* con, struct mg_ws_message* msg) {
	i32 op = mg_json_get_long(msg->data, "$.op", -1);
	if (op != 0)
		return;

	i32 ver = mg_json_get_long(msg->data, "$.d.rpcVersion", 1);
	char payload[128];
	mg_snprintf(payload, sizeof(payload), "{%m:%d,%m:{%m:%d}}",
				mg_print_esc, 0, "op", 1,
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "rpcVersion", ver);
	mg_ws_send(con, payload, strlen(payload), WEBSOCKET_OP_TEXT);
	printf("[INFO] OBS estalished.\n");
}

void respond_identified(struct mg_connection* con, struct mg_ws_message* msg) {
	i32 op = mg_json_get_long(msg->data, "$.op", -1);
	if (op != 2)
		return;

	// Mark connection as established
	ctx.identified = true;
	printf("[INFO] OBS identified.\n");
}

void handle_scene_list(struct mg_connection* con, struct mg_ws_message* msg) {
	i32 op = mg_json_get_long(msg->data, "$.op", -1);
	if (op != 7)
		return;
	char* req_type = mg_json_get_str(msg->data, "$.d.requestType");
	if (!req_type)
		return;
	i32 flag = strcmp("GetSceneList", req_type);
	free(req_type);
	if (flag != 0)
		return;

	u32 total_len = 1;
	for (int i = 0; ; ++i) {
		char path[64];
		mg_snprintf(path, sizeof(path), "$.d.responseData.scenes[%d].sceneName", i);
		char* s = mg_json_get_str(msg->data, path);
		if (!s) break;
		total_len += strlen(s) + 1;
		free(s);
	}

	ctx.data = (char*)malloc(total_len + 1);
	if (!ctx.data) return;

	ctx.data[0] = '/';
	ctx.data[1] = '\0';

	for (int i = 0; ; ++i) {
		char path[64];
		mg_snprintf(path, sizeof(path), "$.d.responseData.scenes[%d].sceneName", i);
		char* s = mg_json_get_str(msg->data, path);
		if (!s) break;
		strcat(ctx.data, s);
		strcat(ctx.data, "/");
		free(s);
	}

	ctx.data_len = total_len;
	ctx.task_complete = true;
}

void handle_scene_creation_switching(struct mg_connection* con, struct mg_ws_message* msg) {
	i32 op = mg_json_get_long(msg->data, "$.op", -1);
	if (op != 7)
		return;
	char* req_type = mg_json_get_str(msg->data, "$.d.requestType");
	if (!req_type)
		return;
	i32 is_cr = strcmp("CreateScene", req_type);
	i32 is_sw = strcmp("SetCurrentProgramScene", req_type);
	i32 is_start = strcmp("StartRecord", req_type);
	i32 is_stop = strcmp("StopRecord", req_type);
	free(req_type);
	if (is_cr != 0 && is_sw != 0 && is_start != 0 && is_stop != 0)
		return;

	ctx.data = malloc(1);
	ctx.data_len = 1;
	ctx.task_complete = true;
}

void fn(struct mg_connection* con, i32 ev, void* ev_data) {
	if (ev == MG_EV_WS_MSG) {
		respond_hello(con, ev_data);
		respond_identified(con, ev_data);
		handle_scene_list(con, ev_data);
		handle_scene_creation_switching(con, ev_data);
	}
}

i32 init_conn() {
	mg_log_set(MG_LL_ERROR);
	mg_mgr_init(&mgr);
	struct mg_connection* con = mg_ws_connect(&mgr, url, fn, NULL, NULL);
	if (!con)
		return 1;
	ctx.con = con;
	while (!ctx.identified) {
		mg_mgr_poll(&mgr, 1000);
	}
	return 0;
}

bool is_scene_exists(const char* scene_name) {
	if (!ctx.identified) {
		return false;
	}

	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "GetSceneList",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData");

	ctx.task_complete = false;
	mg_ws_send(ctx.con, payload, strlen(payload), WEBSOCKET_OP_TEXT);
	while (!ctx.task_complete)
		mg_mgr_poll(&mgr, 1000);

	if (ctx.data_len == 0)
		return false;
	
	i32 offset = strstr(ctx.data, scene_name);

	free(ctx.data);
	ctx.data_len = 0;

	return offset;
}

i32 create_scene(const char* scene_name) {
	if (!ctx.identified) {
		return false;
	}

	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{%m:%m}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "CreateScene",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData",
				mg_print_esc, 0, "sceneName", mg_print_esc, 0, scene_name);
	ctx.task_complete = false;
	mg_ws_send(ctx.con, payload, strlen(payload), WEBSOCKET_OP_TEXT);
	while (!ctx.task_complete)
		mg_mgr_poll(&mgr, 1000);

	if (ctx.data_len == 0)
		return -1;

	free(ctx.data);
	ctx.data_len = 0;

	return 0;
}

i32 switch_scene(const char* scene_name) {
	if (!ctx.identified) {
		return false;
	}

	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{%m:%m}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "SetCurrentProgramScene",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData",
				mg_print_esc, 0, "sceneName", mg_print_esc, 0, scene_name);
	ctx.task_complete = false;
	mg_ws_send(ctx.con, payload, strlen(payload), WEBSOCKET_OP_TEXT);
	while (!ctx.task_complete)
		mg_mgr_poll(&mgr, 1000);

	if (ctx.data_len == 0)
		return -1;

	free(ctx.data);
	ctx.data_len = 0;

	return 0;
}

i32 start_recording() {
	if (!ctx.identified) {
		return false;
	}

	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "StartRecord",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData");
	ctx.task_complete = false;
	mg_ws_send(ctx.con, payload, strlen(payload), WEBSOCKET_OP_TEXT);
	while (!ctx.task_complete)
		mg_mgr_poll(&mgr, 1000);

	if (ctx.data_len == 0)
		return -1;

	free(ctx.data);
	ctx.data_len = 0;

	return 0;
}

i32 stop_recording() {
	if (!ctx.identified) {
		return false;
	}

	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "StopRecord",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData");
	ctx.task_complete = false;
	mg_ws_send(ctx.con, payload, strlen(payload), WEBSOCKET_OP_TEXT);
	while (!ctx.task_complete)
		mg_mgr_poll(&mgr, 1000);

	if (ctx.data_len == 0)
		return -1;

	free(ctx.data);
	ctx.data_len = 0;

	return 0;
}

void free_conn() {
	mg_mgr_free(&mgr);
}
