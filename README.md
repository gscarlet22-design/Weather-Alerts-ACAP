# Weather Monitor ACAP

[![Tests](https://github.com/gscarlet22-design/Weather-Alerts-ACAP/actions/workflows/test.yml/badge.svg)](https://github.com/gscarlet22-design/Weather-Alerts-ACAP/actions/workflows/test.yml)
[![Build](https://github.com/gscarlet22-design/Weather-Alerts-ACAP/actions/workflows/build.yml/badge.svg)](https://github.com/gscarlet22-design/Weather-Alerts-ACAP/actions/workflows/build.yml)
[![Python 3.11+](https://img.shields.io/badge/python-3.11%2B-blue.svg)](https://www.python.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![ACAP v4](https://img.shields.io/badge/ACAP-v4%20containerized-brightgreen.svg)](https://developer.axis.com/acap/)

A containerized **ACAP v4** application that polls the [US National Weather Service API](https://www.weather.gov/documentation/services-web-api) for real-time conditions and severe weather alerts, then:

- **Fires VAPIX Virtual Input events** so you can trigger Action Rules (audio clips, MQTT messages, relay outputs, etc.) directly from the Axis web interface.
- **Renders a dynamic text overlay** on the live video stream showing current conditions and any active alerts (cameras only — gracefully disabled on speakers/intercoms).
- **Exposes a local web UI** on port 8080 for zero-SSH configuration.

---

## Hardware Requirements

| Device Type | Supported | Notes |
|---|---|---|
| Axis IP cameras (ARTPEC-7/8, CV25) | ✅ Full | Events + video overlay |
| Axis network speakers (D3110, C3003-E) | ✅ Partial | Events only, overlay skipped |
| Axis intercoms (A8207-VE, etc.) | ✅ Partial | Events only, overlay skipped |
| Axis cameras without Docker Engine | ❌ | Requires AXIS OS 10.9+ with container support |

**Minimum firmware:** AXIS OS 10.9 (for Docker / containerized ACAP support).

---

## Quick Start

### 1 — Download and install the .eap

Grab the latest `weather-acap-vX.Y.Z.eap` from the [Releases page](https://github.com/your-org/weather-acap/releases).

In the Axis device web interface:
1. Go to **Apps → Add app**
2. Upload the `.eap` file → **Install**
3. Click the toggle to **Start** the app

### 2 — Open the config UI

```
http://<device-ip>:8080
```

Enter your ZIP code (and optionally pin exact coordinates for large ZIP areas), choose which alert types should trigger events, and click **Save**.

The app starts polling immediately. The first weather fetch happens within 30 seconds.

### 3 — Wire up Action Rules

In the Axis web interface, go to **System → Events → Action rules → Add rule**:

| Alert type | Virtual input port |
|---|---|
| Tornado Warning | Port 20 |
| Severe Thunderstorm Warning | Port 21 |
| Flash Flood Warning | Port 22 |
| *(next enabled type)* | Port 23 … |

The port numbering starts at the `Virtual port start` value you configured (default: **20**). Ports activate when an alert becomes active and deactivate when it clears.

---

## Configuration Reference

All settings are available via the web UI at `http://<device-ip>:8080`. They can also be set as environment variables (env vars take precedence over the UI).

| Setting | Env Var | Default | Description |
|---|---|---|---|
| ZIP code | `ZIP_CODE` | *(empty)* | US ZIP code for weather lookups |
| Latitude override | `LAT_OVERRIDE` | *(none)* | Exact decimal latitude — overrides ZIP centroid |
| Longitude override | `LON_OVERRIDE` | *(none)* | Exact decimal longitude — overrides ZIP centroid |
| Enabled alert types | *(UI only)* | See below | Which NWS alert types fire events |
| Virtual port start | `VIRTUAL_PORT_START` | `20` | First VAPIX virtual input port to use |
| Overlay enabled | `OVERLAY_ENABLED` | `true` | Draw text overlay on video (cameras only) |
| Overlay position | `OVERLAY_POSITION` | `topLeft` | `topLeft`, `topRight`, `bottomLeft`, `bottomRight` |
| Poll interval | `POLL_INTERVAL_SECONDS` | `300` | Seconds between NWS API calls (minimum 60) |
| Mock mode | `MOCK_MODE` | `false` | Read from `mock_weather.json` instead of NWS |
| Log level | `LOG_LEVEL` | `INFO` | `DEBUG`, `INFO`, `WARNING`, `ERROR` |
| VAPIX host | `VAPIX_HOST` | `localhost` | Device hostname/IP for VAPIX calls |
| VAPIX username | `VAPIX_USER` | `root` | Axis account with Operator+ role |
| VAPIX password | `VAPIX_PASS` | *(empty)* | VAPIX account password |

**Default enabled alert types:**
- Tornado Warning
- Severe Thunderstorm Warning
- Flash Flood Warning

All NWS alert types are selectable in the UI. Up to 8 alert types can be simultaneously active (virtual ports 20–27 by default).

---

## Development

### Run tests locally

```bash
# Install dependencies
pip install -r app/requirements.txt -r tests/requirements-test.txt

# Run full test suite with coverage
pytest tests/ -v --cov=app --cov-report=term-missing
```

### Simulate on a real device without live NWS

```bash
# Dry run — prints all steps, no VAPIX calls made
python demo.py --dry-run

# Against a real Axis device using mock weather data
python demo.py --host 192.168.1.100 --user root --pass yourpassword
```

### Build the ARM64 Docker image

```bash
docker buildx build --platform linux/arm64 -t weather-acap:dev --load .
```

### Package into an .eap (requires Axis CV SDK)

```bash
docker buildx build --platform linux/arm64 --output type=docker,dest=image.tar .
docker run --rm -v "$(pwd):/workspace" -w /workspace \
    axisecp/acap-computer-vision-sdk:3.3-aarch64 acap-build .
```

---

## Video Overlay

When the device has video capability, the overlay is rendered at the configured corner and updated every poll cycle:

```
[TORNADO WARNING | FLASH FLOOD WARNING] Temp: 91°F | Mostly Cloudy | Wind: 22mph SSW | Humidity: 68%
```

Alert prefixes are automatically omitted when no configured alerts are active.

---

## Troubleshooting

See **[README-Troubleshooting.md](README-Troubleshooting.md)** for:
- Viewing Docker logs on the device (SSH and web UI)
- Verifying virtual port state via cURL
- Verifying overlay registration via VAPIX
- Common Digest auth errors and how to fix them
- How to run `demo.py` against a live device

---

## License

[MIT](LICENSE) — see the LICENSE file for details.
