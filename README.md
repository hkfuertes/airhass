# AirHass Home Assistant add-on repository

AirHass exposes Home Assistant `media_player` entities as AirPlay targets. It runs on the Home Assistant host, advertises targets over mDNS, and uses the Supervisor-provided API token—no long-lived access token is needed.

## Credits

AirHass is based on the AirConnect/AirCast codebase by Philippe (`philippe_44`): https://github.com/philippe44/AirConnect
This fork adds Home Assistant `media_player` discovery and AirPlay-to-Home-Assistant playback control.

## Install

1. Push this repository to a Git URL.
2. In Home Assistant, open **Settings → Add-ons → Add-on Store → ⋮ → Repositories**.
3. Add that Git URL, install **AirHass**, and start it.

## Configuration

The page has one optional multi-select field: **Hide duplicate platforms** (`exclude_platforms`). It offers `cast`, `sonos`, `dlna_dmr`, `kodi`, `mpd`, `plex`, and `yamaha_musiccast`. Native `apple_tv` and `airplay` targets are always hidden.

The add-on auto-selects the LAN interface, uses FLAC, and reads the scoped `SUPERVISOR_TOKEN` directly in C; no token is stored on disk. The final image is `scratch` and contains only the statically linked AirHass binary.

While streaming, AirHass also writes the current track onto the `media_player` entity itself (`media_title`, `media_artist`, `media_album_name`, cover) — the owning integration may overwrite those attributes until the next track.

## Requirements

- Home Assistant OS or Supervised, with add-on support.
- `amd64` or `aarch64` host.
- The target speaker must be able to reach the Home Assistant host on the LAN; Home Assistant does not proxy the audio stream.

## Standalone Docker

Outside the add-on, AirHass needs a direct Home Assistant URL and a long-lived access token instead of `SUPERVISOR_TOKEN`. GitHub Actions publishes the prebuilt image to GHCR (`ghcr.io/hkfuertes/airhass`), so no local compile is needed:

```sh
HA_TOKEN=<long-lived-token> docker compose up
```

`compose.yaml` sets `HA_URL`/`HA_TOKEN` and `network_mode: host` (required for AirPlay/mDNS, Linux only). If only one of `HA_URL`/`HA_TOKEN` is set, AirHass refuses to start. `SUPERVISOR_TOKEN` only works with the add-on's internal Supervisor proxy, not standalone.

## Local checks

```sh
make test
docker build --build-arg BUILD_ARCH=amd64 -t airhass .
```
