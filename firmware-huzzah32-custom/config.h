#pragma once

struct WifiNetwork {
  const char* ssid;
  const char* password;
};

static constexpr WifiNetwork WIFI_NETWORKS[] = {
  {"RUT241_C041", "Xf9u1H2E"},
  {"Stop Animal Cruelty - Go Vegan", "Rmt4ypnnjN7vcxnh"}
};

#define SERVER_URL "https://horse-collar.sandro-raess.ch/api"
#define HORSE_ID         "6a120cd3377aa365b26a8cf8"

// NTP
#define NTP_SERVER       "pool.ntp.org"
#define TZ_OFFSET        3600  // UTC+1 (Switzerland)
