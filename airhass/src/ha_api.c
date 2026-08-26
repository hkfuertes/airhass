/*
 *  AirHass: Home Assistant API minimal client
 *
 *  (c) AirHass contributors
 *
 * See LICENSE
 */

/* ponytail: zero project-specific deps so this file compiles for the test
 * runner without the full INCLUDE path.  Log via fprintf(stderr,...). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#include "jansson.h"
#include "ha_api.h"

#define HA_DEFAULT_PORT 8123
#define HA_ENTITY_PREFIX "media_player."

/*----------------------------------------------------------------------------*/
static bool ha_url_join_path(const ha_url_t *url, const char *path, char *out, size_t out_len) {
	const char *base = url->path;
	const char *suffix = path ? path : "";
	int len;

	if (!*base) len = snprintf(out, out_len, "%s", *suffix ? suffix : "/");
	else len = snprintf(out, out_len, "%s%s%s", base, *suffix && suffix[0] != '/' ? "/" : "", suffix);
	return len >= 0 && (size_t) len < out_len;
}

/*----------------------------------------------------------------------------*/
static bool ha_http_request(const char *method, const char *url, const char *token, const char *path,
                            const char *body_in, const char *content_type,
                            char **body_out, int *status_out, char *line, size_t line_len) {
	ha_url_t u;
	char *resp = NULL, *req = NULL, *body;
	size_t used = 0, size = 0;
	int fd = -1;
	bool ok = false;
	const char *body_data = body_in ? body_in : "";
	size_t body_len = strlen(body_data);

	if (body_out) *body_out = NULL;
	if (status_out) *status_out = 0;
	char request_path[sizeof(u.path)];

	if (line && line_len) *line = '\0';
	if (!ha_url_parse(url, &u) || !ha_url_join_path(&u, path, request_path, sizeof(request_path))) return false;

	char port_str[8];
	snprintf(port_str, sizeof(port_str), "%d", u.port);

	struct addrinfo hints = {0}, *res = NULL;
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(u.host, port_str, &hints, &res) != 0) {
		fprintf(stderr, "[ha] ERROR: cannot resolve host '%s': %s\n",
		        u.host, strerror(errno));
		return false;
	}

	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) {
		fprintf(stderr, "[ha] ERROR: socket: %s\n", strerror(errno));
		freeaddrinfo(res);
		return false;
	}

	if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
		fprintf(stderr, "[ha] ERROR: cannot connect to %s:%d: %s\n",
		        u.host, u.port, strerror(errno));
		freeaddrinfo(res);
		close(fd);
		return false;
	}
	freeaddrinfo(res);

	if (content_type && *content_type) {
		if (asprintf(&req,
		             "%s %s HTTP/1.0\r\n"
		             "Host: %s:%d\r\n"
		             "Authorization: Bearer %s\r\n"
		             "Content-Type: %s\r\n"
		             "Content-Length: %zu\r\n"
		             "Connection: close\r\n"
		             "\r\n"
		             "%s",
		             method, request_path, u.host, u.port, token, content_type, body_len, body_data) < 0) goto done;
	} else {
		if (asprintf(&req,
		             "%s %s HTTP/1.0\r\n"
		             "Host: %s:%d\r\n"
		             "Authorization: Bearer %s\r\n"
		             "Connection: close\r\n"
		             "\r\n",
		             method, request_path, u.host, u.port, token) < 0) goto done;
	}

	if (send(fd, req, strlen(req), 0) < 0) {
		fprintf(stderr, "[ha] ERROR: send: %s\n", strerror(errno));
		goto done;
	}

	while (1) {
		char buf[2048];
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		if (n < 0) {
			fprintf(stderr, "[ha] ERROR: recv: %s\n", strerror(errno));
			goto done;
		}
		if (n == 0) break;
		if (used + (size_t) n + 1 > size) {
			char *tmp;
			size = (used + (size_t) n + 1) * 2;
			tmp = realloc(resp, size);
			if (!tmp) goto done;
			resp = tmp;
		}
		memcpy(resp + used, buf, (size_t) n);
		used += (size_t) n;
		resp[used] = '\0';
	}

	if (!resp) goto done;

	body = strstr(resp, "\r\n\r\n");
	if (!body) {
		fprintf(stderr, "[ha] ERROR: malformed HTTP response\n");
		goto done;
	}
	*body = '\0';
	body += 4;

	if (status_out) {
		char *code = strchr(resp, ' ');
		if (code) *status_out = atoi(code + 1);
	}

	if (line && line_len) {
		char *eol = strstr(resp, "\r\n");
		size_t copy = eol ? (size_t) (eol - resp) : strlen(resp);
		if (copy >= line_len) copy = line_len - 1;
		memcpy(line, resp, copy);
		line[copy] = '\0';
	}

	if (body_out) {
		*body_out = strdup(body);
		if (!*body_out) goto done;
	}

	ok = true;

done:
	if (fd >= 0) close(fd);
	free(req);
	free(resp);
	return ok;
}

/*----------------------------------------------------------------------------*/
static bool ha_http_get(const char *url, const char *token, const char *path,
                        char **body_out, int *status_out, char *line, size_t line_len) {
	return ha_http_request("GET", url, token, path, NULL, NULL, body_out, status_out, line, line_len);
}

/*----------------------------------------------------------------------------*/
static bool ha_http_post_json(const char *url, const char *token, const char *path, const char *json,
                              char **body_out, int *status_out, char *line, size_t line_len) {
	return ha_http_request("POST", url, token, path, json, "application/json",
	                       body_out, status_out, line, line_len);
}

/*----------------------------------------------------------------------------*/
static bool ha_send_all(int fd, const void *buf, size_t len) {
	const char *p = buf;
	while (len) {
		ssize_t n = send(fd, p, len, 0);
		if (n <= 0) return false;
		p += n;
		len -= (size_t) n;
	}
	return true;
}

/*----------------------------------------------------------------------------*/
static bool ha_ws_send_text(int fd, const char *text) {
	const unsigned char mask[4] = {0x12, 0x34, 0x56, 0x78};
	size_t len = strlen(text);
	unsigned char hdr[14];
	size_t hlen = 0;

	hdr[hlen++] = 0x81;
	if (len < 126) {
		hdr[hlen++] = 0x80 | (unsigned char) len;
	} else if (len <= 65535) {
		hdr[hlen++] = 0x80 | 126;
		hdr[hlen++] = (unsigned char) (len >> 8);
		hdr[hlen++] = (unsigned char) len;
	} else {
		return false;
	}
	memcpy(hdr + hlen, mask, sizeof(mask));
	hlen += sizeof(mask);
	if (!ha_send_all(fd, hdr, hlen)) return false;

	char *masked = malloc(len ? len : 1);
	if (!masked) return false;
	for (size_t i = 0; i < len; i++) masked[i] = text[i] ^ mask[i % 4];
	bool ok = ha_send_all(fd, masked, len);
	free(masked);
	return ok;
}

/*----------------------------------------------------------------------------*/
static char *ha_ws_recv_text(int fd) {
	unsigned char hdr[2], ext[8], mask[4];
	uint64_t len;
	bool masked;
	char *out;

	if (recv(fd, hdr, 2, MSG_WAITALL) != 2) return NULL;
	if ((hdr[0] & 0x0f) == 0x8) return NULL;
	if ((hdr[0] & 0x0f) != 0x1) return NULL;
	masked = !!(hdr[1] & 0x80);
	len = hdr[1] & 0x7f;
	if (len == 126) {
		if (recv(fd, ext, 2, MSG_WAITALL) != 2) return NULL;
		len = ((uint64_t) ext[0] << 8) | ext[1];
	} else if (len == 127) {
		return NULL;
	}
	if (masked && recv(fd, mask, 4, MSG_WAITALL) != 4) return NULL;
	out = malloc((size_t) len + 1);
	if (!out) return NULL;
	if (recv(fd, out, (size_t) len, MSG_WAITALL) != (ssize_t) len) {
		free(out);
		return NULL;
	}
	if (masked) for (uint64_t i = 0; i < len; i++) out[i] ^= mask[i % 4];
	out[len] = '\0';
	return out;
}

/*----------------------------------------------------------------------------*/
static int ha_ws_connect(const char *url) {
	ha_url_t u;
	char *req = NULL, resp[2048];
	size_t used = 0;
	int fd = -1;

	char request_path[sizeof(u.path)];

	if (!ha_url_parse(url, &u) ||
	    !ha_url_join_path(&u, !strcmp(u.path, "/core") ? "/websocket" : "/api/websocket",
	                      request_path, sizeof(request_path))) return -1;
	char port_str[8];
	snprintf(port_str, sizeof(port_str), "%d", u.port);

	struct addrinfo hints = {0}, *res = NULL;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(u.host, port_str, &hints, &res) != 0) return -1;
	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
		if (fd >= 0) close(fd);
		freeaddrinfo(res);
		return -1;
	}
	freeaddrinfo(res);

	if (asprintf(&req,
	             "GET %s HTTP/1.1\r\n"
	             "Host: %s:%d\r\n"
	             "Upgrade: websocket\r\n"
	             "Connection: Upgrade\r\n"
	             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
	             "Sec-WebSocket-Version: 13\r\n\r\n",
	             request_path, u.host, u.port) < 0) {
		close(fd);
		return -1;
	}
	if (!ha_send_all(fd, req, strlen(req))) {
		free(req);
		close(fd);
		return -1;
	}
	free(req);

	while (used + 1 < sizeof(resp)) {
		ssize_t n = recv(fd, resp + used, 1, 0);
		if (n <= 0) break;
		used += (size_t) n;
		resp[used] = '\0';
		if (strstr(resp, "\r\n\r\n")) break;
	}
	if (strncmp(resp, "HTTP/1.1 101", 12) && strncmp(resp, "HTTP/1.0 101", 12)) {
		close(fd);
		return -1;
	}
	return fd;
}

/*----------------------------------------------------------------------------*/
static bool ha_entity_id_list_contains(char ids[][HA_ENTITY_ID_LEN], int count, const char *id) {
	for (int i = 0; i < count; i++) if (!strcmp(ids[i], id)) return true;
	return false;
}

/*----------------------------------------------------------------------------*/
static bool ha_platform_in_csv(const char *csv, const char *platform) {
	char token[64];
	const char *p = csv;
	if (!csv || !platform || !*platform) return false;
	while (*p) {
		size_t n = strcspn(p, ",");
		if (n < sizeof(token)) {
			memcpy(token, p, n);
			token[n] = '\0';
			if (!strcmp(token, platform)) return true;
		}
		p += n;
		if (*p == ',') p++;
	}
	return false;
}

/*----------------------------------------------------------------------------*/
static int ha_fetch_hidden_entity_ids(const char *url, const char *token,
                                      char ids[][HA_ENTITY_ID_LEN], int max,
                                      const char *hidden_platforms) {
	int fd = ha_ws_connect(url), count = 0;
	char *auth = NULL;
	if (fd < 0) return -1;

	if (asprintf(&auth, "{\"type\":\"auth\",\"access_token\":\"%s\"}", token) < 0) goto fail;
	if (!ha_ws_send_text(fd, auth)) goto fail;
	free(auth);
	auth = NULL;

	for (int i = 0; i < 4; i++) {
		char *msg = ha_ws_recv_text(fd);
		bool ok = msg && strstr(msg, "\"type\":\"auth_ok\"");
		free(msg);
		if (ok) break;
		if (i == 3) goto fail;
	}

	if (!ha_ws_send_text(fd, "{\"id\":1,\"type\":\"config/entity_registry/list_for_display\"}")) goto fail;
	for (int i = 0; i < 4; i++) {
		char *msg = ha_ws_recv_text(fd);
		json_error_t error;
		json_t *root, *result, *entities;
		if (!msg) goto fail;
		root = json_loads(msg, 0, &error);
		free(msg);
		if (!root) continue;
		result = json_object_get(root, "result");
		entities = result ? json_object_get(result, "entities") : NULL;
		if (!json_is_array(entities)) {
			json_decref(root);
			continue;
		}
		for (size_t n = 0; n < json_array_size(entities) && count < max; n++) {
			json_t *e = json_array_get(entities, n);
			const char *entity_id = json_string_value(json_object_get(e, "ei"));
			const char *platform = json_string_value(json_object_get(e, "pl"));
			if (!entity_id || !platform || strncmp(entity_id, HA_ENTITY_PREFIX, strlen(HA_ENTITY_PREFIX))) continue;
			if (!ha_platform_in_csv(hidden_platforms, platform)) continue;
			snprintf(ids[count++], HA_ENTITY_ID_LEN, "%s", entity_id);
		}
		json_decref(root);
		close(fd);
		return count;
	}

fail:
	free(auth);
	close(fd);
	return -1;
}

/*----------------------------------------------------------------------------*/
bool ha_list_media_player_platforms(const char *url, const char *token) {
	int fd = ha_ws_connect(url), platform_count = 0;
	char platforms[64][64];
	int counts[64] = {0};
	char *auth = NULL;
	if (fd < 0) return false;

	if (asprintf(&auth, "{\"type\":\"auth\",\"access_token\":\"%s\"}", token) < 0) goto fail;
	if (!ha_ws_send_text(fd, auth)) goto fail;
	free(auth);
	auth = NULL;

	for (int i = 0; i < 4; i++) {
		char *msg = ha_ws_recv_text(fd);
		bool ok = msg && strstr(msg, "\"type\":\"auth_ok\"");
		free(msg);
		if (ok) break;
		if (i == 3) goto fail;
	}

	if (!ha_ws_send_text(fd, "{\"id\":1,\"type\":\"config/entity_registry/list_for_display\"}")) goto fail;
	for (int i = 0; i < 4; i++) {
		char *msg = ha_ws_recv_text(fd);
		json_error_t error;
		json_t *root, *result, *entities;
		if (!msg) goto fail;
		root = json_loads(msg, 0, &error);
		free(msg);
		if (!root) continue;
		result = json_object_get(root, "result");
		entities = result ? json_object_get(result, "entities") : NULL;
		if (!json_is_array(entities)) {
			json_decref(root);
			continue;
		}
		for (size_t n = 0; n < json_array_size(entities); n++) {
			json_t *e = json_array_get(entities, n);
			const char *entity_id = json_string_value(json_object_get(e, "ei"));
			const char *platform = json_string_value(json_object_get(e, "pl"));
			if (!entity_id || !platform || strncmp(entity_id, HA_ENTITY_PREFIX, strlen(HA_ENTITY_PREFIX))) continue;
			if (!strcmp(platform, "apple_tv") || !strcmp(platform, "airplay")) continue;
			int p;
			for (p = 0; p < platform_count; p++) if (!strcmp(platforms[p], platform)) break;
			if (p == platform_count && platform_count < (int)(sizeof(platforms) / sizeof(platforms[0])))
				snprintf(platforms[platform_count++], sizeof(platforms[0]), "%s", platform);
			if (p < (int)(sizeof(counts) / sizeof(counts[0]))) counts[p]++;
		}
		puts("HA media_player platform ids:");
		for (int p = 0; p < platform_count; p++) printf("  %s (%d)\n", platforms[p], counts[p]);
		json_decref(root);
		close(fd);
		return true;
	}

fail:
	free(auth);
	close(fd);
	return false;
}

/*----------------------------------------------------------------------------*/
static bool ha_call_service(const char *url, const char *token, const char *path,
                            const char *payload, const char *entity_id) {
	char line[128] = "", *body = NULL;
	int status = 0;

	if (!ha_http_post_json(url, token, path, payload, &body, &status, line, sizeof(line))) return false;
	if (status == 401) {
		fprintf(stderr, "[ha] ERROR: Home Assistant rejected the token (HTTP 401) for %s\n", url);
		free(body);
		return false;
	}
	if (status / 100 != 2) {
		fprintf(stderr, "[ha] ERROR: %s failed for %s: %s\n",
		        path, entity_id ? entity_id : "(null)", *line ? line : "unknown response");
		if (body && *body) fprintf(stderr, "[ha] ERROR: response body: %s\n", body);
		free(body);
		return false;
	}

	free(body);
	return true;
}

/*----------------------------------------------------------------------------*/
bool ha_url_parse(const char *url, ha_url_t *out) {
	if (!url || !*url) {
		fprintf(stderr, "[ha] ERROR: empty URL\n");
		return false;
	}
	if (strncmp(url, "http://", 7) != 0) {
		fprintf(stderr, "[ha] ERROR: only http:// supported, got: %.32s\n", url);
		return false;
	}

	const char *after_scheme = url + 7;
	const char *slash = strchr(after_scheme, '/');
	const char *colon = strchr(after_scheme, ':');
	const char *authority_end = slash ? slash : after_scheme + strlen(after_scheme);
	char *port_end;
	long port;

	if (colon && colon > authority_end) colon = NULL;
	if (colon) {
		port = strtol(colon + 1, &port_end, 10);
		if (port_end != authority_end || port <= 0 || port > 65535) {
			fprintf(stderr, "[ha] ERROR: invalid port in URL\n");
			return false;
		}
		out->port = (int) port;
		authority_end = colon;
	} else {
		out->port = HA_DEFAULT_PORT;
	}

	if (authority_end == after_scheme || authority_end - after_scheme >= (int) sizeof(out->host)) {
		fprintf(stderr, "[ha] ERROR: host too long or empty\n");
		return false;
	}
	memcpy(out->host, after_scheme, (size_t) (authority_end - after_scheme));
	out->host[authority_end - after_scheme] = '\0';

	if (!slash) {
		out->path[0] = '\0';
	} else {
		size_t path_len = strlen(slash);
		while (path_len > 1 && slash[path_len - 1] == '/') path_len--;
		if (path_len >= sizeof(out->path)) {
			fprintf(stderr, "[ha] ERROR: path too long\n");
			return false;
		}
		memcpy(out->path, slash, path_len);
		out->path[path_len] = '\0';
		if (!strcmp(out->path, "/")) out->path[0] = '\0';
	}

	return true;
}

/*----------------------------------------------------------------------------*/
int ha_parse_media_players(const char *json, ha_entity_t *out, int max) {
	json_error_t error;
	json_t *root = json_loads(json ? json : "", 0, &error);
	int count = 0;

	if (!root || !json_is_array(root)) {
		fprintf(stderr, "[ha] ERROR: cannot parse /api/states JSON: %s\n", error.text);
		if (root) json_decref(root);
		return -1;
	}

	for (size_t i = 0; i < json_array_size(root) && count < max; i++) {
		json_t *item = json_array_get(root, i);
		json_t *entity = json_object_get(item, "entity_id");
		const char *entity_id = json_is_string(entity) ? json_string_value(entity) : NULL;
		if (!entity_id || strncmp(entity_id, HA_ENTITY_PREFIX, strlen(HA_ENTITY_PREFIX))) continue;

		json_t *attrs = json_object_get(item, "attributes");
		json_t *friendly = attrs ? json_object_get(attrs, "friendly_name") : NULL;
		const char *name = json_is_string(friendly) ? json_string_value(friendly) : entity_id;

		snprintf(out[count].entity_id, sizeof(out[count].entity_id), "%s", entity_id);
		snprintf(out[count].name, sizeof(out[count].name), "%s", name);
		snprintf(out[count].udn, sizeof(out[count].udn), "ha:%s", entity_id);
		count++;
	}

	json_decref(root);
	return count;
}

/*----------------------------------------------------------------------------*/
int ha_fetch_media_players(const char *url, const char *token, ha_entity_t *out, int max,
                           const char *hidden_platforms) {
	char *body = NULL, line[128] = "";
	int status = 0, count, hidden_count = 0;
	char hidden[max > 0 ? max : 1][HA_ENTITY_ID_LEN];

	if (!ha_http_get(url, token, "/api/states", &body, &status, line, sizeof(line))) return -1;
	if (status == 401) {
		fprintf(stderr, "[ha] ERROR: Home Assistant rejected the token (HTTP 401) for %s\n", url);
		free(body);
		return -1;
	}
	if (status / 100 != 2) {
		fprintf(stderr, "[ha] ERROR: Home Assistant /api/states failed: %s\n", *line ? line : "unknown response");
		free(body);
		return -1;
	}

	count = ha_parse_media_players(body, out, max);
	free(body);
	if (count <= 0) return count;

	hidden_count = ha_fetch_hidden_entity_ids(url, token, hidden, max, hidden_platforms);
	if (hidden_count > 0) {
		int kept = 0;
		for (int i = 0; i < count; i++) {
			if (ha_entity_id_list_contains(hidden, hidden_count, out[i].entity_id)) continue;
			if (kept != i) out[kept] = out[i];
			kept++;
		}
		count = kept;
	}
	return count;
}

/*----------------------------------------------------------------------------*/
bool ha_parse_media_player_state(const char *json, ha_media_player_state_t *out) {
	json_error_t error;
	json_t *root;
	json_t *state;
	json_t *attrs;
	json_t *volume;
	const char *state_str;

	if (!out) return false;
	memset(out, 0, sizeof(*out));

	root = json_loads(json ? json : "", 0, &error);
	if (!root || !json_is_object(root)) {
		fprintf(stderr, "[ha] ERROR: cannot parse media_player state JSON: %s\n", error.text);
		if (root) json_decref(root);
		return false;
	}

	state = json_object_get(root, "state");
	state_str = json_is_string(state) ? json_string_value(state) : "";
	snprintf(out->state, sizeof(out->state), "%s", state_str);

	attrs = json_object_get(root, "attributes");
	volume = attrs ? json_object_get(attrs, "volume_level") : NULL;
	if (json_is_real(volume) || json_is_integer(volume)) {
		out->has_volume_level = true;
		out->volume_level = ha_volume_level(json_number_value(volume));
	}

	json_decref(root);
	return true;
}

/*----------------------------------------------------------------------------*/
bool ha_fetch_media_player_state(const char *url, const char *token, const char *entity_id,
                                 ha_media_player_state_t *out) {
	char *body = NULL, line[128] = "", path[HA_ENTITY_ID_LEN + 32];
	int status = 0;
	bool ok = false;

	if (!entity_id || !*entity_id || !out) return false;
	if (snprintf(path, sizeof(path), "/api/states/%s", entity_id) >= (int) sizeof(path)) return false;
	if (!ha_http_get(url, token, path, &body, &status, line, sizeof(line))) return false;
	if (status == 401) {
		fprintf(stderr, "[ha] ERROR: Home Assistant rejected the token (HTTP 401) for %s\n", url);
		goto done;
	}
	if (status / 100 != 2) {
		fprintf(stderr, "[ha] ERROR: Home Assistant %s failed: %s\n", path, *line ? line : "unknown response");
		goto done;
	}

	ok = ha_parse_media_player_state(body, out);

done:
	free(body);
	return ok;
}

/*----------------------------------------------------------------------------*/
ha_raop_event_t ha_state_to_raop_event(const char *state, ha_raop_event_t current) {
	if (state && !strcasecmp(state, "playing")) {
		return current == HA_RAOP_PLAY ? HA_RAOP_NONE : HA_RAOP_PLAY;
	}
	if (state && !strcasecmp(state, "paused")) {
		return current == HA_RAOP_PLAY ? HA_RAOP_PAUSE : HA_RAOP_NONE;
	}
	if (state && (!strcasecmp(state, "idle") || !strcasecmp(state, "off") || !strcasecmp(state, "unavailable"))) {
		return current == HA_RAOP_STOP ? HA_RAOP_NONE : HA_RAOP_STOP;
	}

	return HA_RAOP_NONE;
}

/*----------------------------------------------------------------------------*/
const char *ha_codec_extension(const char *codec_config) {
	if (codec_config && strcasestr(codec_config, "mp3")) return "mp3";
	if (codec_config && strcasestr(codec_config, "aac")) return "aac";
	if (codec_config && strcasestr(codec_config, "wav")) return "wav";
	return "flac";
}

/*----------------------------------------------------------------------------*/
const char *ha_codec_content_type(const char *codec_config) {
	if (codec_config && strcasestr(codec_config, "mp3")) return "audio/mpeg";
	if (codec_config && strcasestr(codec_config, "aac")) return "audio/aac";
	if (codec_config && strcasestr(codec_config, "wav")) return "audio/wav";
	return "audio/flac";
}

/*----------------------------------------------------------------------------*/
bool ha_build_play_media_payload(const char *entity_id, const char *media_content_id,
                                 const char *media_content_type, char *out, size_t out_len) {
	json_t *root;
	char *json;
	bool ok;

	if (!entity_id || !*entity_id || !media_content_id || !*media_content_id ||
	    !media_content_type || !*media_content_type || !out || !out_len) return false;

	root = json_pack("{ssssss}",
	                 "entity_id", entity_id,
	                 "media_content_id", media_content_id,
	                 "media_content_type", media_content_type);
	if (!root) return false;

	json = json_dumps(root, JSON_COMPACT);
	json_decref(root);
	if (!json) return false;

	ok = snprintf(out, out_len, "%s", json) < (int) out_len;
	free(json);
	return ok;
}

/*----------------------------------------------------------------------------*/
bool ha_build_entity_payload(const char *entity_id, char *out, size_t out_len) {
	json_t *root;
	char *json;
	bool ok;

	if (!entity_id || !*entity_id || !out || !out_len) return false;

	root = json_pack("{ss}", "entity_id", entity_id);
	if (!root) return false;

	json = json_dumps(root, JSON_COMPACT);
	json_decref(root);
	if (!json) return false;

	ok = snprintf(out, out_len, "%s", json) < (int) out_len;
	free(json);
	return ok;
}

/*----------------------------------------------------------------------------*/
double ha_volume_level(double level) {
	if (level < 0) return 0;
	if (level > 1) return 1;
	return level;
}

/*----------------------------------------------------------------------------*/
bool ha_build_volume_payload(const char *entity_id, double volume_level, char *out, size_t out_len) {
	json_t *root;
	char *json;
	bool ok;

	if (!entity_id || !*entity_id || !out || !out_len) return false;

	root = json_pack("{sssf}", "entity_id", entity_id, "volume_level", ha_volume_level(volume_level));
	if (!root) return false;

	json = json_dumps(root, JSON_COMPACT);
	json_decref(root);
	if (!json) return false;

	ok = snprintf(out, out_len, "%s", json) < (int) out_len;
	free(json);
	return ok;
}

/*----------------------------------------------------------------------------*/
bool ha_play_media(const char *url, const char *token, const char *entity_id,
                   const char *media_content_id, const char *media_content_type) {
	char payload[1024];

	if (!ha_build_play_media_payload(entity_id, media_content_id, media_content_type, payload, sizeof(payload))) {
		fprintf(stderr, "[ha] ERROR: cannot build media_player.play_media payload for %s\n",
		        entity_id ? entity_id : "(null)");
		return false;
	}

	if (!ha_call_service(url, token, "/api/services/media_player/play_media", payload, entity_id)) {
		fprintf(stderr,
		        "[ha] ERROR: check the entity supports direct URL playback and the speaker can reach %s\n",
		        media_content_id ? media_content_id : "(null)");
		return false;
	}

	return true;
}

/*----------------------------------------------------------------------------*/
bool ha_stop_media(const char *url, const char *token, const char *entity_id) {
	char payload[256];

	if (!ha_build_entity_payload(entity_id, payload, sizeof(payload))) {
		fprintf(stderr, "[ha] ERROR: cannot build media_player.media_stop payload for %s\n",
		        entity_id ? entity_id : "(null)");
		return false;
	}

	return ha_call_service(url, token, "/api/services/media_player/media_stop", payload, entity_id);
}

/*----------------------------------------------------------------------------*/
bool ha_set_volume(const char *url, const char *token, const char *entity_id, double volume_level) {
	char payload[256];

	if (!ha_build_volume_payload(entity_id, volume_level, payload, sizeof(payload))) {
		fprintf(stderr, "[ha] ERROR: cannot build media_player.volume_set payload for %s\n",
		        entity_id ? entity_id : "(null)");
		return false;
	}

	return ha_call_service(url, token, "/api/services/media_player/volume_set", payload, entity_id);
}

/*----------------------------------------------------------------------------*/
bool ha_ping(const char *url, const char *token) {
	int status = 0;
	char line[128] = "";

	if (!ha_http_get(url, token, "/api/", NULL, &status, line, sizeof(line))) return false;
	if (status == 200) {
		fprintf(stderr, "[ha] INFO: Home Assistant API reachable and authorised at %s\n", url);
		return true;
	}
	if (status == 401) {
		fprintf(stderr, "[ha] ERROR: Home Assistant rejected the token (HTTP 401) for %s\n", url);
		return false;
	}
	if (status / 100 == 2) {
		fprintf(stderr, "[ha] INFO: Home Assistant API responded OK (%s)\n", line);
		return true;
	}

	fprintf(stderr, "[ha] ERROR: unexpected response from Home Assistant: %s\n", *line ? line : "(empty response)");
	return false;
}
