// === Includes ===
#include <string.h>
#include "obs.h"

// === Globals ===
static const char* obs_ws_url = "ws://127.0.0.1:4455";

typedef struct ObsWsContext {
	bool identified;
	bool task_complete;
	struct mg_connection* con;
	char* data;
	u64 data_len;
} ObsWsContext;

struct mg_mgr obs_mgr;
ObsWsContext obs_ctx = { false, false, NULL, NULL, 0 };

// === WebSocket message handlers ===
// Handle OBS WebSocket "Hello" to negotiate RPC version.
void handle_hello_op(struct mg_connection* con, struct mg_ws_message* msg) {
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

// Mark the connection as identified after OBS accepts the handshake.
void handle_identified_op(struct mg_connection* con, struct mg_ws_message* msg) {
	i32 op = mg_json_get_long(msg->data, "$.op", -1);
	if (op != 2)
		return;

	// Mark connection as established
	obs_ctx.identified = true;
}

// Build a slash-delimited scene list string to allow substring checks.
void handle_scene_list_response(struct mg_connection* con, struct mg_ws_message* msg) {
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

	obs_ctx.data = (char*)malloc(total_len);

	obs_ctx.data[0] = '/';
	obs_ctx.data[1] = '\0';

	for (int i = 0; ; ++i) {
		char path[64];
		mg_snprintf(path, sizeof(path), "$.d.responseData.scenes[%d].sceneName", i);
		char* s = mg_json_get_str(msg->data, path);
		if (!s) break;
		strcat_s(obs_ctx.data, total_len, s);
		strcat_s(obs_ctx.data, total_len, "/");
		free(s);
	}

	log_debug("received scene list: %s", obs_ctx.data);
	obs_ctx.data_len = total_len;
	obs_ctx.task_complete = true;
}

// Handle one-shot request responses (create/switch/start/stop).
void handle_simple_request_response(struct mg_connection* con, struct mg_ws_message* msg) {
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
		obs_ctx.data = malloc(1);
		obs_ctx.data_len = 1;
	} else {
		if (comment) {
			log_error("%s request failed: %s", req_type, comment);
		}
	}
	free(req_type);
	free(comment);
	obs_ctx.task_complete = true;
}

// Dispatch OBS WebSocket messages to the relevant handlers.
void obs_ws_event_handler(struct mg_connection* con, i32 ev, void* ev_data) {
	if (ev == MG_EV_WS_MSG) {
		handle_hello_op(con, ev_data);
		handle_identified_op(con, ev_data);
		handle_scene_list_response(con, ev_data);
		handle_simple_request_response(con, ev_data);
	}
}

// Poll in fixed slices until the flag equals the expected value.
// Uses OBS_CONNECT_TIMEOUT_MS as the total time budget (unchanged).
void obs_poll_while_flag_equals(bool* flag, bool expected_value) {
	i32 max_iters = (OBS_CONNECT_TIMEOUT_MS + 99) / 100;
	for (i32 i = 0; i < max_iters && *flag != expected_value; ++i) {
		mg_mgr_poll(&obs_mgr, 100);
	}
}

// === Connection lifecycle ===
// Open the OBS WebSocket connection and wait until identified.
i32 obs_connect() {
	mg_log_set(MG_LL_ERROR);
	mg_mgr_init(&obs_mgr);
	struct mg_connection* con = mg_ws_connect(&obs_mgr, obs_ws_url, obs_ws_event_handler, NULL, NULL);
	if (!con) {
		log_fatal("could not create OBS websocket connection");
		return 1;
	}
	obs_ctx.con = con;

	obs_poll_while_flag_equals(&obs_ctx.identified, true);
	if (!obs_ctx.identified) {
		log_fatal("OBS websocket connection timed out after %d ms", OBS_CONNECT_TIMEOUT_MS);
		obs_disconnect();
		obs_ctx.con = NULL;
		return 1;
	}

	log_info("OBS websocket connection identified");
	return 0;
}

// Send a request and block until the response handler marks completion.
i32 obs_send_request(char* payload) {
	if (!obs_ctx.identified) {
		log_fatal("OBS websocket connection is not identified");
		return 1;
	}
	obs_ctx.task_complete = false;

	mg_ws_send(obs_ctx.con, payload, strlen(payload), WEBSOCKET_OP_TEXT);
	obs_poll_while_flag_equals(&obs_ctx.task_complete, true);

	return 0;
}

// Free any response payload stored in the context.
void obs_reset_response() {
	if (obs_ctx.data)
		free(obs_ctx.data);
	obs_ctx.data = NULL;
	obs_ctx.data_len = 0;
}

// === OBS request helpers ===
i32 obs_scene_exists(const char* scene_name, bool* exists) {
	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "GetSceneList",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData");

	i32 err = obs_send_request(payload);
	
	if (!err) {
		char pattern[128];
		sprintf_s(pattern, sizeof(pattern), "/%s/", scene_name);
		*exists = strstr(obs_ctx.data, pattern);
	}

	obs_reset_response();
	return err;
}

i32 obs_create_scene(const char* scene_name) {
	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{%m:%m}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "CreateScene",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData",
				mg_print_esc, 0, "sceneName", mg_print_esc, 0, scene_name);
	i32 err = obs_send_request(payload);
	obs_reset_response();
	return err;
}

i32 obs_set_current_scene(const char* scene_name) {
	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{%m:%m}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "SetCurrentProgramScene",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData",
				mg_print_esc, 0, "sceneName", mg_print_esc, 0, scene_name);
	i32 err = obs_send_request(payload);
	obs_reset_response();
	return err;
}

i32 obs_start_recording() {
	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "StartRecord",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData");
	i32 err = obs_send_request(payload);
	obs_reset_response();
	return err;
}

i32 obs_stop_recording() {
	char payload[1024];
	mg_snprintf(payload, sizeof(payload), "{%m:6,%m:{%m:%m,%m:%m,%m:{}}}",
				mg_print_esc, 0, "op",
				mg_print_esc, 0, "d",
				mg_print_esc, 0, "requestType", mg_print_esc, 0, "StopRecord",
				mg_print_esc, 0, "requestId", mg_print_esc, 0, "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c",
				mg_print_esc, 0, "requestData");
	i32 err = obs_send_request(payload);
	obs_reset_response();
	return err;
}

// === Shutdown ===
void obs_disconnect() {
	mg_mgr_free(&obs_mgr);
}
