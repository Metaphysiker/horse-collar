#pragma once

// Add as many networks as needed
#define WIFI_NETWORKS { \
  { "your-home-ssid",   "your-home-password"   }, \
  { "your-stable-ssid", "your-stable-password" }, \
}
#define SERVER_URL       "http://your-server-ip:8080"
#define HORSE_ID         "your-horse-objectid"

// NTP
#define NTP_SERVER       "pool.ntp.org"
#define TZ_OFFSET        3600  // UTC+1 (Switzerland)
