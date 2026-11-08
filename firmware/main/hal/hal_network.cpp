/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <stackchan/stackchan.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <wifi_manager.h>
#include <board.h>
#include <settings.h>
#include <mutex>
#include <queue>
#include <vector>
#include <ctime>
#include <sys/time.h>
#include <esp_sntp.h>
#include <atomic>

static std::string _tag = "Network";

static void time_sync_notification_cb(struct timeval*)
{
    mclog::tagInfo(_tag, "SNTP time synchronized");
    GetHAL().syncSystemTimeToRtc();
}

void Hal::startSntp()
{
    mclog::tagInfo(_tag, "SNTP init");

    if (esp_sntp_enabled()) {
    } else {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.google.com");
        esp_sntp_setservername(2, "cn.pool.ntp.org");

        sntp_set_time_sync_notification_cb(time_sync_notification_cb);

        esp_sntp_init();
    }
}

void Hal::startNetwork(std::function<void(std::string_view)> onLog)
{
    // A network diagnostic may already have connected saved Wi-Fi. Starting
    // the active station again emits no new Connected event, so reuse it.
    if (WifiManager::GetInstance().IsConnected()) {
        mclog::tagInfo(_tag, "network already connected");
        {
            Settings settings("app_config", true);
            settings.SetBool("is_configed", true);
        }
        startSntp();
        return;
    }

    std::atomic<bool> network_connected = false;
    std::atomic<bool> in_config_mode    = false;
    bool timeout_logged                 = false;

    auto& board = Board::GetInstance();
    mclog::tagInfo(_tag, "start and wait for network connected...");

    board.SetNetworkEventCallback(
        [&network_connected, &in_config_mode, &onLog](NetworkEvent event, const std::string& data) {
            switch (event) {
                case NetworkEvent::Scanning:
                    if (onLog) {
                        onLog("WiFi scanning...");
                    }
                    break;
                case NetworkEvent::Connecting: {
                    if (data.empty()) {
                        if (onLog) {
                            onLog("WiFi connecting...");
                        }
                    } else {
                        if (onLog) {
                            onLog(fmt::format("Connecting to {} ...", data));
                        }
                    }
                    break;
                }
                case NetworkEvent::Connected: {
                    network_connected = true;
                    break;
                }
                case NetworkEvent::Disconnected:
                    break;
                case NetworkEvent::WifiConfigModeEnter:
                    // SoftAP hotspot is active — wait for the user to provision WiFi.
                    // Do not reboot into BLE App setup.
                    in_config_mode = true;
                    if (onLog) {
                        onLog("Connect to the SoftAP hotspot and open the config page in a browser.");
                    }
                    break;
                case NetworkEvent::WifiConfigModeExit:
                    in_config_mode = false;
                    break;
                // Cellular modem specific events
                case NetworkEvent::ModemDetecting:
                    break;
                case NetworkEvent::ModemErrorNoSim:
                    break;
                case NetworkEvent::ModemErrorRegDenied:
                    break;
                case NetworkEvent::ModemErrorInitFailed:
                    break;
                case NetworkEvent::ModemErrorTimeout:
                    break;
            }
        });
    const uint32_t started_at = millis();
    board.StartNetwork();

    while (!network_connected && !WifiManager::GetInstance().IsConnected()) {
        const bool config_mode = in_config_mode || WifiManager::GetInstance().IsConfigMode();
        // While SoftAP is up, wait indefinitely for the user to finish provisioning.
        // Only log once if station connect times out before SoftAP starts.
        if (!config_mode && uint32_t(millis() - started_at) >= 65000) {
            if (onLog && !timeout_logged) {
                onLog("WiFi unavailable. Connect to the SoftAP hotspot and configure WiFi in a browser.");
                timeout_logged = true;
            }
        }
        GetHAL().delay(50);
    }
    mclog::tagInfo(_tag, "network connected");
    {
        Settings settings("app_config", true);
        settings.SetBool("is_configed", true);
    }
    board.SetNetworkEventCallback(nullptr);

    startSntp();
}

WifiStatus Hal::getWifiStatus()
{
    auto& wifi = WifiManager::GetInstance();

    if (wifi.IsConfigMode()) {
        return WifiStatus::None;
    }
    if (!wifi.IsConnected()) {
        return WifiStatus::None;
    }

    int rssi = wifi.GetRssi();
    if (rssi >= -65) {
        return WifiStatus::High;
    } else if (rssi >= -75) {
        return WifiStatus::Medium;
    }
    return WifiStatus::Low;
}
