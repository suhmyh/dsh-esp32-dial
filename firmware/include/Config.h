// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// Build-time configuration for the DSH status dial.
//
// DESIGN: everything here is a *fallback seed*, not a hard requirement. The
// device stores its real settings (WiFi, bridge host/token) in NVS and they can
// be changed at runtime two ways:
//
//   1. Provisioning portal — if the stored WiFi fails, the dial starts its own
//      access point (name and password shown on screen). Join it from any
//      phone, open 192.168.4.1, enter network + bridge details. No reflash.
//   2. Serial console — `settings` prints the current config, `set wifi:...`
//      type commands update it live (see Provision.cpp).
//
// So the macros below matter only on the very first boot. Leaving SSID/TOKEN
// blank is intentional: the first boot then skips straight to provisioning.

#pragma once

// ── First-boot WiFi seed (stored to NVS, then editable) ────────────────
// Seeded with the network this dial normally lives on, so a reflash does not
// mean re-provisioning. NVS still wins once anything has been saved there:
// these values apply only when NVS is empty, and the portal or serial console
// can overwrite them at any time.
#define DSH_DEFAULT_WIFI_SSID       "IPhone 17 Pro Max"
#define DSH_DEFAULT_WIFI_PASSWORD   "1122334455"

// ── First-boot bridge seed (stored to NVS, then editable) ──────────────
// The dial reaches the bridge through the FRP tunnel, not over the LAN: the
// machine running DSH is in another building. 7002 is the public port that
// forwards to the bridge's local 3083.
#define DSH_DEFAULT_BRIDGE_HOST     "149.88.88.167"
#define DSH_DEFAULT_BRIDGE_PORT     7002
#define DSH_DEFAULT_BRIDGE_TOKEN    "fdec2108ebe9f617eca8cde2ca64f999"

// ── Provisioning portal ─────────────────────────────────────────────────
// The dial becomes an access point while unconfigured. Its name and password
// are derived from the chip's MAC so they are unique per device and stable
// across reboots; both are shown on the dial screen while provisioning.
#define DSH_AP_SSID_PREFIX      "DSH-Dial"
// A phone scanning the QR code on screen can join without typing anything.
#define DSH_AP_PASSWORD_LEN     8
// When the stored config is missing or blank, the portal waits indefinitely:
// there is nothing to fall back to. When WiFi worked before and then failed,
// the portal reverts after this long so the dial resumes its job on its own
// once the network returns (a router reboot should not need a person).
#define DSH_AP_FALLBACK_TIMEOUT_MS  300000

// ── Behaviour ─────────────────────────────────────────────────────────────
// A state frame older than this is drawn as `stale` rather than live.
#define DSH_FRESHNESS_MS    3000

// How long WiFi must report "not connected" before the dial believes it.
// WL_DISCONNECTED appears transiently during beacon misses and DHCP renewals,
// and the ESP32's own auto-reconnect fixes those within about a second. Acting
// on the first reading tore down a working link; waiting this long does not.
#define DSH_LINK_GRACE_MS   5000

// Heartbeat period. The bridge answers each ping with a pong.
#define DSH_PING_MS         10000

// How long to wait for any data from the bridge before timing out the socket.
// The bridge sends a state frame at least every tick (1 s), so 3× the ping
// interval means about 30 seconds of silence. Must be longer than the ping
// interval so that normal operation never triggers it.
#define DSH_WS_SILENCE_MS   (DSH_PING_MS * 3)

// Reconnect backoff bounds (milliseconds).
#define DSH_BACKOFF_MIN_MS  1000
#define DSH_BACKOFF_MAX_MS  30000

// Idle screen dimming. 0 disables dimming entirely.
#define DSH_DIM_AFTER_MS    60000
#define DSH_DIM_BRIGHTNESS  15
#define DSH_FULL_BRIGHTNESS 90

// An unanswered ask keeps the screen lit and chiming this long.
#define DSH_ASK_TIMEOUT_MS  120000

// NVS namespace holding the runtime-editable settings.
#define DSH_NVS_NAMESPACE   "dsh-dial"