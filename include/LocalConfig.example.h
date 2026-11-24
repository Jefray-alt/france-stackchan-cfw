#pragma once

// Copy to include/LocalConfig.h (ignored by Git), then enter local settings.
#define ROBOT_WIFI_SSID "your-wifi-name"
#define ROBOT_WIFI_PASSWORD "your-wifi-password"
// HTTP only, numeric LAN IPv4, optional port/trailing slash, no path or query.
// This is the computer running backend/server.mjs, not localhost on CoreS3.
#define ROBOT_TRANSCRIPTION_BASE_URL "http://192.168.1.23:3000"
