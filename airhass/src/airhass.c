/*
 *  AirHass: Home Assistant to AirPlay
 *
 *  (c) Philippe, philippe_44@outlook.com
 *
 * See LICENSE
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <locale.h>
#ifdef _WIN32
#include <process.h>
#endif

#include "cross_net.h"
#include "cross_util.h"
#include "cross_thread.h"
#include "cross_log.h"

#include "airhass.h"
#include "mdnssvc.h"
#include "config_ha.h"
#include "ixml.h"
#include "ha_api.h"

#define DISCOVERY_TIME 	20
#define MEDIA_VOLUME	0.5
#define HA_POLL		2000

/*----------------------------------------------------------------------------*/
/* globals */
/*----------------------------------------------------------------------------*/
struct sMR	*glMRDevices;
uint16_t	glPortBase, glPortRange, glPicoPort;
int32_t		glLogLimit = -1;
int			glMaxDevices = 32;
char		glBinding[16] = "?";
char		glHAUrl[STR_LEN]   = "";
char		glHAToken[STR_LEN] = "";

log_level	main_loglevel = lINFO;
log_level	raop_loglevel = lINFO;
log_level	util_loglevel = lWARN;
log_level	ha_loglevel = lINFO;

tMRConfig			glMRConfig = {
							true,	// enabled
							"",		// name
							"flac",	// use_flac
							true,	// metadata
							true,	// flush
							MEDIA_VOLUME,	// media volume (0..1)
							{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
							"",		// rtp/http_latency (0 = use client's request)
							false,	// drift
							"", 	// artwork
					};



/*----------------------------------------------------------------------------*/
/* consts or pseudo-const*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/
static log_level*			loglevel = &main_loglevel;
#if LINUX || FREEBSD || SUNOS
static bool					glDaemonize = false;
#endif
static bool					glMainRunning = true;
static struct in_addr 		glHost;
static pthread_t 			glMainThread;
static char*				glLogFile;
static bool					glDiscovery = false;
static bool					glInteractive = true;
static char*				glPidFile = NULL;
static bool					glAutoSaveConfigFile = false;
static char					glHiddenHAPlatforms[512] = "apple_tv,airplay";
static bool					glListHAPlatforms = false;
static bool					glGracefullShutdown = true;
static void*				glConfigID = NULL;
static char					glConfigName[STR_LEN] = "./config.xml";
static struct mdnsd*		glmDNSServer = NULL;
static uint32_t				glNetmask;
static char*				glNameFormat = "%s";

static char usage[] =
			VERSION "\n"
		   "See -t for license terms\n"
		   "Usage: [options]\n"
		   "  -b <ip|iface>network  address or interface to bind to\n"
		   "  -a <port>[:<count>]   set inbound port and range for RTP and HTTP\n"
		   "  -c <mp3[:<rate>]|aac[:<rate>]|flac[:0..9][/1152...16384]|wav>\taudio format send to player\n"
   		   "  -v <0..1>             default media volume factor\n"
		   "  -x <config file>      read config from file (default is ./config.xml)\n"
		   "  -i <config file>      discover players, save <config file> and exit\n"
		   "  -I                    auto save config after discovery\n"
		   "  -N <format>           transform device name using C format (%s=name)\n"
		   "  -l <[rtp][:http][:f]> RTP and HTTP latency (ms), ':f' forces silence fill\n"
		   "  -r                    let timing reference drift (no click)\n"
		   "  -f <logfile>          write debug to logfile\n"
		   "  -p <pid file>         write PID in file\n"
		   "  -d <log>=<level>      set logging level, logs: all|raop|main|util|ha, level: error|warn|info|debug|sdebug\n"
#if LINUX || FREEBSD
		   "  -z                    daemonize\n"
#endif
		   "  -Z                    NOT interactive\n"
		   "  -k                    immediate exit on SIGQUIT and SIGTERM\n"
		   "  -t                    license terms\n"
   		   "  --noflush             ignore flush command (wait for teardown to stop)\n"
   		   "  --list-ha-platforms   list HA media_player platform ids and exit\n"
   		   "  --no-ha-platform=x,y  hide HA entity-registry platforms by id (default: apple_tv,airplay)\n"
		   "\n"
		   "Build options:"
#if LINUX
		   " LINUX"
#endif
#if WIN
		   " WIN"
#endif
#if OSX
		   " OSX"
#endif
#if FREEBSD
		   " FREEBSD"
#endif
#if EVENTFD
		   " EVENTFD"
#endif
#if SELFPIPE
		   " SELFPIPE"
#endif
#if WINEVENT
		   " WINEVENT"
#endif
		   "\n\n";

static char license[] =
		   "This program is free software: you can redistribute it and/or modify\n"
		   "it under the terms of the GNU General Public License as published by\n"
		   "the Free Software Foundation, either version 3 of the License, or\n"
		   "(at your option) any later version.\n\n"
		   "This program is distributed in the hope that it will be useful,\n"
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		   "GNU General Public License for more details.\n\n"
		   "You should have received a copy of the GNU General Public License\n"
		   "along with this program.  If not, see <http://www.gnu.org/licenses/>.\n\n"
	;


/*----------------------------------------------------------------------------*/
/* prototypes */
/*----------------------------------------------------------------------------*/
static void *HAThread(void *args);
static bool  AddHADevice(struct sMR *Device, const ha_entity_t *Entity);
static bool  AddHADevices(void);
static bool	 Start(bool cold);
static bool	 Stop(bool exit);

/*----------------------------------------------------------------------------*/
static char *MakeStreamUrl(const char *codec_config, uint16_t port) {
	char *uri = NULL;
	static int count;
	const char *codec = ha_codec_extension(codec_config);

	(void)!asprintf(&uri, "http://%s:%u/stream-%u.%s", inet_ntoa(glHost), port, count++, codec);
	return uri;
}

/*----------------------------------------------------------------------------*/
static raopsr_event_t HARaopEvent(ha_raop_event_t event) {
	switch (event) {
		case HA_RAOP_PLAY: return RAOP_PLAY;
		case HA_RAOP_PAUSE: return RAOP_PAUSE;
		case HA_RAOP_STOP: return RAOP_STOP;
		default: return RAOP_STREAM;
	}
}

/*----------------------------------------------------------------------------*/
static enum eMRstate HAPlayerState(const char *state) {
	if (state && !strcasecmp(state, "playing")) return PLAYING;
	if (state && !strcasecmp(state, "paused")) return PAUSED;
	return STOPPED;
}

/*----------------------------------------------------------------------------*/
static void raop_cb(void *owner, raopsr_event_t event, ...) {
	struct sMR *Device = (struct sMR*) owner;
	const char *entity_id = !strncmp(Device->UDN, "ha:", 3) ? Device->UDN + 3 : Device->UDN;
	va_list args;
	va_start(args, event);

	pthread_mutex_lock(&Device->Mutex);

	// this is async, so player might have been deleted
	if (!Device->Running) {
		LOG_WARN("[%p]: device has been removed", owner);
		pthread_mutex_unlock(&Device->Mutex);
		va_end(args);
		return;
	}

	switch (event) {
		case RAOP_STREAM:
			LOG_INFO("[%p]: Stream", Device);
			Device->RaopState = event;
			break;
		case RAOP_STOP:
			LOG_INFO("[%p]: Stop", Device);
			ha_stop_media(glHAUrl, glHAToken, entity_id);
			Device->RaopState = event;
			break;
		case RAOP_FLUSH:
			if (Device->Config.Flush) {
				LOG_INFO("[%p]: Flush", Device);
				ha_stop_media(glHAUrl, glHAToken, entity_id);
				Device->RaopState = event;
			}
			break;
		case RAOP_PLAY:
			LOG_INFO("[%p]: Play", Device);
			if (Device->RaopState != RAOP_PLAY) {
				uint16_t port = va_arg(args, uint32_t);
				char *uri = MakeStreamUrl(Device->Config.Codec, port);

				if (uri) {
					if (ha_play_media(glHAUrl, glHAToken, entity_id, uri,
				                  ha_codec_content_type(Device->Config.Codec))) {
						LOG_INFO("[%p]: Home Assistant play_media %s -> %s", Device, entity_id, uri);
					}
					free(uri);
				}
			}
			Device->RaopState = event;
			break;
		case RAOP_VOLUME: {
			double volume = ha_volume_level(va_arg(args, double));

			if (fabs(volume - Device->Volume) >= 0.01) {
				Device->Volume = volume;
				Device->VolumeStampTx = gettime_ms();
				ha_set_volume(glHAUrl, glHAToken, entity_id, volume);
				LOG_INFO("[%p]: Home Assistant volume_set %s -> %0.4lf", Device, entity_id, volume);
			}
			Device->RaopState = event;
			break;
		}
		default:
			break;
	}

	va_end(args);
	pthread_mutex_unlock(&Device->Mutex);
}

/*----------------------------------------------------------------------------*/
static void *HAThread(void *args) {
	struct sMR *p = (struct sMR*) args;
	const char *entity_id = !strncmp(p->UDN, "ha:", 3) ? p->UDN + 3 : p->UDN;

	while (p->Running) {
		ha_media_player_state_t remote = {0};

		if (ha_fetch_media_player_state(glHAUrl, glHAToken, entity_id, &remote)) {
			uint32_t now = gettime_ms();
			enum eMRstate next_state = HAPlayerState(remote.state);
			ha_raop_event_t action = HA_RAOP_NONE;

			pthread_mutex_lock(&p->Mutex);
			if (!p->Running) {
				pthread_mutex_unlock(&p->Mutex);
				break;
			}

			if (next_state != p->State) {
				action = ha_state_to_raop_event(remote.state,
				                               p->RaopState == RAOP_PLAY ? HA_RAOP_PLAY :
				                               p->RaopState == RAOP_STOP ? HA_RAOP_STOP : HA_RAOP_NONE);
				p->State = next_state;
			}

			if (action != HA_RAOP_NONE) {
				raopsr_event_t event = HARaopEvent(action);
				LOG_INFO("[%p]: Home Assistant %s -> %s", p, entity_id,
				         event == RAOP_PLAY ? "play" : event == RAOP_PAUSE ? "pause" : "stop");
				raopsr_notify(p->Raop, event, NULL);
				p->RaopState = event;
			}

			if (remote.has_volume_level && fabs(remote.volume_level - p->Volume) >= 0.01 && now > p->VolumeStampTx + 1000) {
				double volume = remote.volume_level;
				p->Volume = volume;
				p->VolumeStampRx = now;
				LOG_INFO("[%p]: Home Assistant volume %s -> %0.4lf", p, entity_id, volume);
				raopsr_notify(p->Raop, RAOP_VOLUME, &volume);
			}

			pthread_mutex_unlock(&p->Mutex);
		}

		crossthreads_sleep(HA_POLL);
	}

	return NULL;
}

/*----------------------------------------------------------------------------*/
static void *MainThread(void *args) {
	while (glMainRunning) {
		crossthreads_sleep(30*1000);
		if (!glMainRunning) break;

		if (glLogFile && glLogLimit != - 1) {
			uint32_t size = ftell(stderr);

			if (size > glLogLimit*1024*1024) {
				uint32_t Sum, BufSize = 16384;
				uint8_t *buf = malloc(BufSize);

				FILE *rlog = fopen(glLogFile, "rb");
				FILE *wlog = fopen(glLogFile, "r+b");
				LOG_DEBUG("Resizing log", NULL);
				for (Sum = 0, fseek(rlog, size - (glLogLimit*1024*1024) / 2, SEEK_SET);
					 (BufSize = fread(buf, 1, BufSize, rlog)) != 0;
					 Sum += BufSize, fwrite(buf, 1, BufSize, wlog));

				Sum = fresize(wlog, Sum);
				fclose(wlog);
				fclose(rlog);
				NFREE(buf);
				if (!freopen(glLogFile, "a", stderr)) {
					LOG_ERROR("re-open error while truncating log", NULL);
				}
			}
		}

		// try to detect IP change when not forced
		if (inet_addr(glBinding) == INADDR_NONE) {
			struct in_addr host;
			host = get_interface(!strchr(glBinding, '?') ? glBinding : NULL, NULL, &glNetmask);
			if (host.s_addr != INADDR_NONE && host.s_addr != glHost.s_addr) {
				LOG_INFO("IP change detected %s", inet_ntoa(glHost));
				Stop(false);
				glMainRunning = true;
				Start(false);
			}
		}

	}

	return NULL;
}

/*----------------------------------------------------------------------------*/
static struct sMR *SearchUDN(const char *UDN) {
	for (int i = 0; i < glMaxDevices; i++) {
		if (glMRDevices[i].Running && !strcmp(glMRDevices[i].UDN, UDN))
			return glMRDevices + i;
	}

	return NULL;
}

/*----------------------------------------------------------------------------*/
static bool AddHADevice(struct sMR *Device, const ha_entity_t *Entity) {
	memcpy(&Device->Config, &glMRConfig, sizeof(tMRConfig));
	LoadMRConfig(glConfigID, (char*) Entity->udn, &Device->Config);
	if (!Device->Config.Enabled) return false;

	strcpy(Device->UDN, Entity->udn);
	Device->Magic		= MAGIC;
	Device->Running		= true;
	Device->State 		= STOPPED;
	Device->Volume 		= 0;
	Device->Raop 		= NULL;
	Device->RaopState	= RAOP_STOP;
	Device->VolumeStampRx = Device->VolumeStampTx = gettime_ms() - 2000;
	Device->Thread = (pthread_t) 0;

	if (!*Device->Config.Name) snprintf(Device->Config.Name, sizeof(Device->Config.Name), "%s", Entity->name);
	snprintf(Device->Name, sizeof(Device->Name), "%s", Entity->name);

	if (!memcmp(Device->Config.mac, "\0\0\0\0\0\0", 6)) {
		memset(Device->Config.mac, 0xcc, 2);
		*(uint32_t*) (Device->Config.mac + 2) = hash32(Device->UDN);
	}

	LOG_INFO("[%p]: adding Home Assistant target (%s - %s)", Device, Device->Config.Name, Entity->entity_id);
	Device->Raop = raopsr_create(glHost, glmDNSServer, Device->Config.Name,
	                            "airhass", Device->Config.mac, Device->Config.Codec,
	                            Device->Config.Metadata, Device->Config.Drift,
	                            Device->Config.Flush, Device->Config.Latency,
	                            Device, raop_cb, NULL, glPortBase, glPortRange, -1);
	if (!Device->Raop) {
		LOG_ERROR("[%p]: cannot create RAOP instance (%s)", Device, Device->Config.Name);
		Device->Running = false;
		return false;
	}

	pthread_create(&Device->Thread, NULL, &HAThread, Device);
	return true;
}

/*----------------------------------------------------------------------------*/
static bool AddHADevices(void) {
	ha_entity_t entities[MAX_RENDERERS];
	int count = ha_fetch_media_players(glHAUrl, glHAToken, entities, MAX_RENDERERS, glHiddenHAPlatforms);
	bool updated = false;

	if (count < 0) return false;
	LOG_INFO("Found %d Home Assistant media_player entities", count);

	for (int i = 0; i < count; i++) {
		struct sMR *Device;

		if (SearchUDN(entities[i].udn)) continue;
		for (Device = glMRDevices; Device < glMRDevices + glMaxDevices && Device->Running; Device++);
		if (Device == glMRDevices + glMaxDevices) {
			LOG_ERROR("Too many devices (max:%u)", glMaxDevices);
			break;
		}

		if (AddHADevice(Device, &entities[i])) updated = true;
	}

	if ((updated && glAutoSaveConfigFile) || glDiscovery) {
		LOG_INFO("Updating configuration %s", glConfigName);
		SaveConfig(glConfigName, glConfigID, false);
	}

	return true;
}

/*----------------------------------------------------------------------------*/
static void FlushDevices(void) {
	for (int i = 0; i < glMaxDevices; i++) {
		struct sMR *p = &glMRDevices[i];
		if (!p->Running) continue;

		raopsr_delete(p->Raop);
		pthread_mutex_lock(&p->Mutex);
		p->Running = false;
		pthread_mutex_unlock(&p->Mutex);
		pthread_join(p->Thread, NULL);
	}
}

/*----------------------------------------------------------------------------*/
static bool Start(bool cold) {
	// must bind to an address
	char* iface = NULL;
	glHost = get_interface(!strchr(glBinding, '?') ? glBinding : NULL, &iface, &glNetmask);
	LOG_INFO("Binding to %s [%s] with mask 0x%08x", inet_ntoa(glHost), iface, ntohl(glNetmask));
	NFREE(iface);

	// can't find a suitable interface
	if (glHost.s_addr == INADDR_NONE) return false;

	if (cold) {
		// mutexes must always be valid
		glMRDevices = calloc(glMaxDevices, sizeof(struct sMR));
		for (int i = 0; i < glMaxDevices; i++) pthread_mutex_init(&glMRDevices[i].Mutex, 0);

		// start the main thread
		pthread_create(&glMainThread, NULL, &MainThread, NULL);
	}

	// init pico httpserver
	glPicoPort = glPortBase;
	http_pico_init(glHost, &glPicoPort, glPicoPort ? glPortRange : 1);
	LOG_INFO("Starting pico HTTP server on port %hu", glPicoPort);

	char hostname[STR_LEN];
	gethostname(hostname, sizeof(hostname));
	strcat(hostname, ".local");

	if ((glmDNSServer = mdnsd_start(glHost, false)) == NULL) return false;
	mdnsd_set_hostname(glmDNSServer, hostname, glHost);

	return true;
}

/*---------------------------------------------------------------------------*/
static bool Stop(bool exit) {
	glMainRunning = false;

	if (glHost.s_addr != INADDR_ANY) {
		LOG_DEBUG("flush renderers ...", NULL);
		FlushDevices();

		// stop advertising devices
		mdnsd_stop(glmDNSServer);
	}

	if (exit) {
		LOG_DEBUG("terminate main thread ...", NULL);
		crossthreads_wake();
		pthread_join(glMainThread, NULL);
		for (int i = 0; i < glMaxDevices; i++) pthread_mutex_destroy(&glMRDevices[i].Mutex);
		// terminate pico http server
		http_pico_close();

		if (glConfigID) ixmlDocument_free(glConfigID);
		netsock_close();
	}

	if (exit) free(glMRDevices);
	return true;
}

/*---------------------------------------------------------------------------*/
static void sighandler(int signum) {
	if (!glGracefullShutdown) {
		LOG_INFO("forced exit", NULL);
		exit(0);
	}

	Stop(true);
	exit(0);
}

/*---------------------------------------------------------------------------*/
static bool HideHAPlatforms(const char *platforms) {
	if (!platforms || !*platforms) return true;
	if (*glHiddenHAPlatforms && strlen(glHiddenHAPlatforms) + 1 < sizeof(glHiddenHAPlatforms)) strcat(glHiddenHAPlatforms, ",");
	if (strlen(glHiddenHAPlatforms) + strlen(platforms) >= sizeof(glHiddenHAPlatforms)) return false;
	strcat(glHiddenHAPlatforms, platforms);
	return true;
}

/*---------------------------------------------------------------------------*/
static bool ParseArgs(int argc, char **argv) {
	char *optarg = NULL;
	int optind = 1;
	char cmdline[256] = "";

	for (int i = 0; i < argc && (strlen(argv[i]) + strlen(cmdline) + 2 < sizeof(cmdline)); i++) {
		strcat(cmdline, argv[i]);
		strcat(cmdline, " ");
	}

	while (optind < argc && strlen(argv[optind]) >= 2 && argv[optind][0] == '-') {
		char *opt = argv[optind] + 1;
		if (strstr("abxdpiflcvN", opt) && optind < argc - 1) {
			optarg = argv[optind + 1];
			optind += 2;
		} else if (strstr("tzZIkr", opt) || opt[0] == '-') {
			optarg = NULL;
			optind += 1;
		}
		else {
			printf("%s", usage);
			return false;
		}

		switch (opt[0]) {
		case 'f':
			glLogFile = optarg;
			break;
		case 'v':
			glMRConfig.MediaVolume = atof(optarg);
			break;
		case 'c':
			strcpy(glMRConfig.Codec, optarg);
			break;
		case 'b':
			strcpy(glBinding, optarg);
			break;
		case 'a':
			sscanf(optarg, "%hu:%hu", &glPortBase, &glPortRange);
			break;
		case 'i':
			strcpy(glConfigName, optarg);
			glDiscovery = true;
			break;
		case 'I':
			glAutoSaveConfigFile = true;
			break;
		case 'p':
			glPidFile = optarg;
			break;
		case 'N':
			glNameFormat = optarg;
			break;
		case 'Z':
			glInteractive = false;
			break;
		case 'k':
			glGracefullShutdown = false;
			break;
		case 'r':
			glMRConfig.Drift = true;
			break;
		case 'l':
			strcpy(glMRConfig.Latency, optarg);
			break;
#if LINUX || FREEBSD
		case 'z':
			glDaemonize = true;
			break;
#endif
		case 'd':
			{
				char *l = strtok(optarg, "=");
				char *v = strtok(NULL, "=");
				log_level new = lWARN;
				if (l && v) {
					if (!strcmp(v, "error"))  new = lERROR;
					if (!strcmp(v, "warn"))   new = lWARN;
					if (!strcmp(v, "info"))   new = lINFO;
					if (!strcmp(v, "debug"))  new = lDEBUG;
					if (!strcmp(v, "sdebug")) new = lSDEBUG;
					if (!strcmp(l, "all") || !strcmp(l, "main")) main_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "util")) util_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "ha")) ha_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "raop")) raop_loglevel = new;
				}
				else {
					printf("%s", usage);
					return false;
				}
			}
			break;
		case 't':
			printf("%s", license);
			return false;
		case '-':
			if (!strcmp(opt + 1, "noflush")) glMRConfig.Flush = false;
			else if (!strcmp(opt + 1, "list-ha-platforms")) glListHAPlatforms = true;
			else if (!strncmp(opt + 1, "no-ha-platform=", 15)) {
				if (!HideHAPlatforms(opt + 16)) {
					printf("%s", usage);
					return false;
				}
			}
			else {
				printf("%s", usage);
				return false;
			}
			break;
		default:
			break;
		}
	}

	return true;
}

/*----------------------------------------------------------------------------*/
/*																			  */
/*----------------------------------------------------------------------------*/
int main(int argc, char *argv[]) {
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
#if defined(SIGQUIT)
	signal(SIGQUIT, sighandler);
#endif
#if defined(SIGHUP)
	signal(SIGHUP, sighandler);
#endif
#if defined(SIGPIPE)
	signal(SIGPIPE, SIG_IGN);
#endif

	// otherwise some atof/strtod fail with '.'
	setlocale(LC_NUMERIC, "C");

	netsock_init();

	// first try to find a config file on the command line
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-x")) {
			strcpy(glConfigName, argv[i+1]);
		}
	}

	// load config from xml file
	glConfigID = (void*) LoadConfig(glConfigName, &glMRConfig);

	// potentially overwrite with some cmdline parameters
	if (!ParseArgs(argc, argv)) exit(1);

	// make sure port range is correct
	if (glPortBase && !glPortRange) glPortRange = glMaxDevices*4;

	if (glLogFile) {
		if (!freopen(glLogFile, "a", stderr)) {
			fprintf(stderr, "error opening logfile %s: %s\n", glLogFile, strerror(errno));
		}
	}

	LOG_WARN("Starting airhass version: %s", VERSION);

	if (strtod("0.30", NULL) != 0.30) {
		LOG_WARN("weird GLIBC, try -static build in case of failure");
	}

	if (!glConfigID) {
		LOG_WARN("no config file, using defaults");
	}

	// validate Home Assistant config when ha_url is set (HA backend active)
	if (*glHAUrl) {
		if (!*glHAToken) {
			LOG_ERROR("ha_url is set but ha_token is missing - cannot start", NULL);
			exit(1);
		}
		LOG_INFO("Checking Home Assistant at %s", glHAUrl);
		if (!ha_ping(glHAUrl, glHAToken)) {
			LOG_ERROR("Home Assistant not reachable at %s - check ha_url and ha_token", glHAUrl);
			exit(1);
		}
		if (glListHAPlatforms) {
			if (!ha_list_media_player_platforms(glHAUrl, glHAToken)) {
				LOG_ERROR("Cannot list Home Assistant media_player platform ids", NULL);
				exit(1);
			}
			return 0;
		}
	}
	else if (glListHAPlatforms) {
		LOG_ERROR("--list-ha-platforms needs ha_url and ha_token", NULL);
		exit(1);
	}

	// just do device discovery and exit
	if (glDiscovery) {
		Start(true);
		if (*glHAUrl && !AddHADevices()) {
			LOG_ERROR("Cannot create Home Assistant targets", NULL);
			Stop(true);
			exit(1);
		}
		sleep(DISCOVERY_TIME + 1);
		Stop(true);
		return(0);
	}

#if LINUX || FREEBSD
	if (glDaemonize) {
		if (daemon(1, glLogFile ? 1 : 0)) {
			fprintf(stderr, "error daemonizing: %s\n", strerror(errno));
		}
	}
#endif

	if (glPidFile) {
		FILE *pid_file;
		pid_file = fopen(glPidFile, "wb");
		if (pid_file) {
			fprintf(pid_file, "%d", (int) getpid());
			fclose(pid_file);
		}
		else {
			LOG_ERROR("Cannot open PID file %s", glPidFile);
		}
	}

	if (!Start(true)) {

		LOG_ERROR("Cannot start", NULL);

		exit(1);

	}

	if (*glHAUrl && !AddHADevices()) {
		LOG_ERROR("Cannot create Home Assistant targets", NULL);
		Stop(true);
		exit(1);
	}

	for (char resp[20] = ""; strcmp(resp, "exit");) {
#if LINUX || FREEBSD || SUNOS
		if (!glDaemonize && glInteractive)
			(void)! scanf("%s", resp);
		else
			pause();
#else
		if (glInteractive)
			(void)! scanf("%s", resp);
		else
#if OSX
			pause();
#else
			Sleep(INFINITE);
#endif
#endif
		char level[20];

		if (!strcmp(resp, "maindbg"))	{
			(void)! scanf("%s", level);
			main_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "utildbg"))	{
			(void)! scanf("%s", level);
			util_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "hadbg"))	{
			(void)! scanf("%s", level);
			ha_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "raopdbg"))	{
			(void)! scanf("%s", level);
			raop_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "save"))	{
			char name[128];
			(void)! scanf("%s", name);
			SaveConfig(name, glConfigID, true);
		}

		if (!strcmp(resp, "dump") || !strcmp(resp, "dumpall"))	{
			bool all = !strcmp(resp, "dumpall");

			for (int i = 0; i < glMaxDevices; i++) {
				struct sMR *p = &glMRDevices[i];
				bool Locked = pthread_mutex_trylock(&p->Mutex);

				if (!Locked) pthread_mutex_unlock(&p->Mutex);
				if (!p->Running && !all) continue;
				printf("%20.20s [r:%u] [l:%u] [s:%u]", p->Config.Name, p->Running,
					   Locked, p->State);
				printf("\n");
			}
		}

	};

	LOG_INFO("stopping Home Assistant targets ...", NULL);
	Stop(true);
	LOG_INFO("all done", NULL);

	return true;
}




