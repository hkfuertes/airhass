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

#include "jansson.h"
#include "ha_api.h"

#define HA_DEFAULT_PORT 8123
#define HA_ENTITY_PREFIX "media_player."

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
	if (line && line_len) *line = '\0';
	if (!ha_url_parse(url, &u)) return false;
	if (path && *path) snprintf(u.path, sizeof(u.path), "%s", path);

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
		             method, u.path, u.host, u.port, token, content_type, body_len, body_data) < 0) goto done;
	} else {
		if (asprintf(&req,
		             "%s %s HTTP/1.0\r\n"
		             "Host: %s:%d\r\n"
		             "Authorization: Bearer %s\r\n"
		             "Connection: close\r\n"
		             "\r\n",
		             method, u.path, u.host, u.port, token) < 0) goto done;
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

	/* find optional path separator */
	const char *slash = strchr(after_scheme, '/');
	/* find optional port separator (must be before slash) */
	const char *colon = strchr(after_scheme, ':');
	if (colon && slash && colon > slash) colon = NULL; /* colon is inside path */

	int host_len;
	if (colon) {
		host_len = (int) (colon - after_scheme);
		out->port = atoi(colon + 1);
		if (out->port <= 0 || out->port > 65535) {
			fprintf(stderr, "[ha] ERROR: invalid port in URL\n");
			return false;
		}
	} else {
		host_len = slash ? (int) (slash - after_scheme) : (int) strlen(after_scheme);
		out->port = HA_DEFAULT_PORT;
	}

	if (host_len <= 0 || host_len >= (int) sizeof(out->host)) {
		fprintf(stderr, "[ha] ERROR: host too long or empty\n");
		return false;
	}
	memcpy(out->host, after_scheme, host_len);
	out->host[host_len] = '\0';

	/* always ping /api/ regardless of any trailing path in the config URL */
	snprintf(out->path, sizeof(out->path), "/api/");

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
int ha_fetch_media_players(const char *url, const char *token, ha_entity_t *out, int max) {
	char *body = NULL, line[128] = "";
	int status = 0, count;

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
	return count;
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
bool ha_play_media(const char *url, const char *token, const char *entity_id,
                   const char *media_content_id, const char *media_content_type) {
	char payload[1024], line[128] = "", *body = NULL;
	int status = 0;

	if (!ha_build_play_media_payload(entity_id, media_content_id, media_content_type, payload, sizeof(payload))) {
		fprintf(stderr, "[ha] ERROR: cannot build media_player.play_media payload for %s\n",
		        entity_id ? entity_id : "(null)");
		return false;
	}

	if (!ha_http_post_json(url, token, "/api/services/media_player/play_media", payload,
	                       &body, &status, line, sizeof(line))) return false;
	if (status == 401) {
		fprintf(stderr, "[ha] ERROR: Home Assistant rejected the token (HTTP 401) for %s\n", url);
		free(body);
		return false;
	}
	if (status / 100 != 2) {
		fprintf(stderr,
		        "[ha] ERROR: media_player.play_media failed for %s: %s -- check the entity supports direct URL playback and the speaker can reach %s\n",
		        entity_id, *line ? line : "unknown response", media_content_id);
		if (body && *body) fprintf(stderr, "[ha] ERROR: response body: %s\n", body);
		free(body);
		return false;
	}

	free(body);
	return true;
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
