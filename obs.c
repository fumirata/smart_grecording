#include <string.h>
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
}

void respond_identified(struct mg_connection* con, struct mg_ws_message* msg) {
	i32 op = mg_json_get_long(msg->data, "$.op", -1);
	if (op != 2)
		return;

	// Mark connection as established
	ctx.identified = true;
}

void handle_scene_list(struct mg_connection* con, struct mg_ws_message* msg) {
	// Only process RequestResponse (op = 7)
	i32 op = mg_json_get_long(msg->data, "$.op", -1);
	if (op != 7)
		return;

	// Make sure it's expected response type
	char* req_type = mg_json_get_str(msg->data, "$.d.requestType");
	if (!req_type)
		return;
	i32 flag = strcmp("GetSceneList", req_type);
	free(req_type);
	if (flag != 0)
		return;

	u64 total_len = 2;
	for (int i = 0; ; ++i) {
		char path[64];
		mg_snprintf(path, sizeof(path), "$.d.responseData.scenes[%d].sceneName", i);
		char* s = mg_json_get_str(msg->data, path);
		if (!s) break;
		total_len += strlen(s) + 1;
		free(s);
	}

	ctx.data = (char*)malloc(total_len);
	if (!ctx.data) return;

	ctx.data[0] = '/';
	ctx.data[1] = '\0';

	for (int i = 0; ; ++i) {
		char path[64];
		mg_snprintf(path, sizeof(path), "$.d.responseData.scenes[%d].sceneName", i);
		char* s = mg_json_get_str(msg->data, path);
		if (!s) break;
		strcat_s(ctx.data, total_len, s);
		strcat_s(ctx.data, total_len, "/");
		free(s);
	}

	log_debug("received scene list: %s", ctx.data);
	ctx.data_len = total_len;
	ctx.task_complete = true;
}

void handle_oneshot_req(struct mg_connection* con, struct mg_ws_message* msg) {
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
	if (is_cr != 0 && is_sw != 0 && is_start != 0 && is_stop != 0)
		return;

	bool req_status = mg_json_get_bool(msg->data, "$.d.requestStatus.result", false);
	char* comment = mg_json_get_str(msg->data, "$.d.requestStatus.comment");
	if (req_status) {
		ctx.data = malloc(1);
		ctx.data_len = 1;
	} else {
		if (comment) {
			log_error("%s request failed because %s", req_type, comment);
		}
	}
	free(req_type);
	free(comment);
	ctx.task_complete = true;
}

void fn(struct mg_connection* con, i32 ev, void* ev_data) {
	if (ev == MG_EV_WS_MSG) {
		respond_hello(con, ev_data);
		respond_identified(con, ev_data);
		handle_scene_list(con, ev_data);
		handle_oneshot_req(con, ev_data);
	}
}

i32 init_conn() {
	mg_log_set(MG_LL_ERROR);
	mg_mgr_init(&mgr);
	struct mg_connection* con = mg_ws_connect(&mgr, url, fn, NULL, NULL);
	if (!con) {
		log_fatal("cannot create the websocket connection\n");
		return 1;
	}
	ctx.con = con;
	while (!ctx.identified) {
		mg_mgr_poll(&mgr, 1000);
	}
	log_info("connection identified");
	return 0;
}

i32 send_req(char* payload) {
	if (!ctx.identified) {
		log_fatal("connection is not identified");
		return 1;
	}
	ctx.task_complete = false;

	mg_ws_send(ctx.con, payload, strlen(payload), WEBSOCKET_OP_TEXT);
	while (!ctx.task_complete)
		mg_mgr_poll(&mgr, 1000);

	return 0;
}

void clean_ctx() {
	if (ctx.data)
		free(ctx.data);
	ctx.data = NULL;
	ctx.data_len = 0;
}


i32 is_scene_exists(const char* scene_name, bool* exists) {
	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "GetSceneList",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData");

	i32 err = send_req(payload);
	
	if (!err) {
		*exists = strstr(ctx.data, scene_name);
	}

	clean_ctx();
	return err;
}

i32 create_scene(const char* scene_name) {
	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{%m:%m}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "CreateScene",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData",
				mg_print_esc, 0, "sceneName", mg_print_esc, 0, scene_name);
	i32 err = send_req(payload);
	clean_ctx();
	return err;
}

i32 switch_scene(const char* scene_name) {
	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{%m:%m}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "SetCurrentProgramScene",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData",
				mg_print_esc, 0, "sceneName", mg_print_esc, 0, scene_name);
	i32 err = send_req(payload);
	clean_ctx();
	return err;
}

i32 start_record() {
	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "StartRecord",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData");
	i32 err = send_req(payload);
	clean_ctx();
	return err;
}

i32 stop_record() {
	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "StopRecord",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData");
	i32 err = send_req(payload);
	clean_ctx();
	return err;
}

void free_conn() {
	mg_mgr_free(&mgr);
}
