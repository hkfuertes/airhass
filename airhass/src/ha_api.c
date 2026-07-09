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

#include "ha_api.h"

#define HA_DEFAULT_PORT 8123

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
		host_len = (int)(colon - after_scheme);
		out->port = atoi(colon + 1);
		if (out->port <= 0 || out->port > 65535) {
			fprintf(stderr, "[ha] ERROR: invalid port in URL\n");
			return false;
		}
	} else {
		host_len = slash ? (int)(slash - after_scheme) : (int)strlen(after_scheme);
		out->port = HA_DEFAULT_PORT;
	}

	if (host_len <= 0 || host_len >= (int)sizeof(out->host)) {
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
bool ha_ping(const char *url, const char *token) {
	ha_url_t u;
	if (!ha_url_parse(url, &u)) return false;

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

	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
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

	char req[1280];
	snprintf(req, sizeof(req),
	         "GET %s HTTP/1.0\r\n"
	         "Host: %s:%d\r\n"
	         "Authorization: Bearer %s\r\n"
	         "Connection: close\r\n"
	         "\r\n",
	         u.path, u.host, u.port, token);

	if (send(fd, req, strlen(req), 0) < 0) {
		fprintf(stderr, "[ha] ERROR: send: %s\n", strerror(errno));
		close(fd);
		return false;
	}

	/* read only the first response line (e.g. "HTTP/1.1 200 OK\r\n") */
	char resp[128] = {0};
	int  total = 0, n;
	while (total < (int)sizeof(resp) - 1) {
		n = (int)recv(fd, resp + total, 1, 0);
		if (n <= 0) break;
		total++;
		if (total >= 2 && resp[total-1] == '\n') break;
	}
	close(fd);

	/* check status code */
	if (strstr(resp, " 200")) {
		fprintf(stderr, "[ha] INFO: Home Assistant API reachable and authorised at %s\n", url);
		return true;
	}
	if (strstr(resp, " 401")) {
		fprintf(stderr, "[ha] ERROR: Home Assistant rejected the token (HTTP 401) for %s\n", url);
		return false;
	}
	/* any other 2xx is fine (shouldn't happen for /api/ but be lenient) */
	if (strstr(resp, " 2")) {
		fprintf(stderr, "[ha] INFO: Home Assistant API responded OK (%s)\n", resp);
		return true;
	}

	fprintf(stderr, "[ha] ERROR: unexpected response from Home Assistant: %s\n", resp);
	return false;
}
