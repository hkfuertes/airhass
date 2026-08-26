# Changelog

## 1.13.0

- Remove now-playing reporting (sensor and media_player attribute injection). Playback, stop and volume only. Leftover `sensor.airhass_*` entities disappear on HA restart.

## 1.12.0

- Write now-playing metadata (title/artist/album/cover) onto the `media_player` entity itself instead of a separate sensor.

## 1.11.1

- Fix: the now-playing sensor shows a friendly name (`<player> Now Playing`) instead of the raw entity id.

## 1.11.0

- Report now-playing metadata to Home Assistant: `sensor.airhass_<player>_now_playing` with artist, album, title and cover.

## 1.10.1

- Initial Home Assistant add-on: Supervisor token, multi-arch `scratch` image, hide-platforms selector, standalone Docker (`HA_URL`/`HA_TOKEN`).
