# AirHass

Minimal macOS runbook for Home Assistant-backed AirPlay targets.

## Credits

AirHass is based on the AirConnect/AirCast codebase by Philippe (`philippe_44`): https://github.com/philippe44/AirConnect
This fork adds Home Assistant `media_player` discovery and AirPlay-to-Home-Assistant playback control.

## What this does

AirHass exposes Home Assistant `media_player.*` entities as AirPlay targets.
When you stream to one of those targets, AirHass tells Home Assistant to `play_media` a direct URL like `http://<your-mac>:<port>/stream-7.flac`.

Important:
- Home Assistant does **not** proxy the audio.
- The speaker/device behind that `media_player` must be able to reach the AirHass URL on your Mac over the LAN.
- `ha_url` currently supports `http://` only.

## macOS quick start

1. Build:
   ```sh
   make
   ```
2. Copy the sample config:
   ```sh
   cp config.sample.xml config.xml
   ```
3. Edit `config.xml`:
   - set `binding` to your Mac's LAN IP or interface
   - set `ha_url` to your Home Assistant URL, usually `http://homeassistant.local:8123`
   - set `ha_token` to a long-lived access token
4. Start AirHass:
   ```sh
   ./bin/airhass-macos -x ./config.xml
   ```
5. On the same LAN, open the AirPlay output picker on your phone/Mac and look for names matching your HA `media_player` entities.

## Minimal config

Use `config.sample.xml` as a template.

Rules:
- `binding` must not be `127.0.0.1` if a real speaker should reach AirHass.
- `ha_url` + `ha_token` are both required when Home Assistant mode is enabled.
- Leave codec as `flac` first; switch to `mp3:320` if the target fails to play the stream.

## Discovering Home Assistant targets

At startup AirHass fetches `/api/states` and turns every `media_player.*` into an AirPlay target.

Useful checks:
- AirHass log should contain `Found <n> Home Assistant media_player entities`
- AirPlay picker should show at least one target
- If zero targets appear, verify the token, URL, and that the HA entity is really `media_player.*`

Optional discovery-only run:
```sh
./bin/airhass-macos -x ./config.xml -i ./config.xml
```
That scans, writes discovered devices back to `config.xml`, and exits.

## Streaming test checklist

1. Start AirHass.
2. Confirm at least one HA-backed target appears in AirPlay.
3. Select that target and play audio.
4. Confirm the real speaker starts playback.
5. Stop playback from the AirPlay sender.
6. Change volume from the AirPlay sender.

Expected behavior:
- stream: AirHass calls `media_player.play_media` with a direct stream URL
- stop/flush: AirHass calls `media_player.media_stop`
- volume: AirHass calls `media_player.volume_set` with a 0..1 level

## Known limitations

- No real-device validation is included here; credentials and speakers are required for that.
- Home Assistant does **not** relay audio bytes. Reachability from the speaker to the Mac is mandatory.
- `ha_url` is `http://` only right now.
- Some HA integrations do not support direct URL playback or ignore stop/volume. If that happens, keep the entity but treat stop/volume as integration-specific limitations.
- FLAC is the default. Try `mp3:320` when playback starts failing, the target refuses the URL, or the integration/device is picky about codecs/container support.

## Human validation still required

Blocked on real credentials/devices:
- confirm a real `media_player` appears as an AirPlay target
- confirm end-to-end playback on a real speaker
- confirm stop and volume behavior for the specific HA integration/device
- capture any device-specific limitations
