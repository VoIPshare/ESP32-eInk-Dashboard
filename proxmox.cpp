#include "app.h"
#include "proxmox.h"
#include <ArduinoJson.h>

#include <HTTPClient.h>  
#include <WiFi.h>
#include <WiFiClientSecure.h>

std::vector<ProxmoxHost> hostsInfo;
int proxmoxNodesCount = 0;

void fetchProxmoxStates(LayoutItem* item, int maxRetries = 3) {
    DBG(F("[Proxmox]: Fetching Proxmox"));

    hostsInfo.clear();
    proxmoxNodesCount = 0;

    WiFiClientSecure client;
    client.stop();
    client.setInsecure();

    char url[160];

    // Extra1 = host:port
    snprintf(url, sizeof(url),
             "https://%s/api2/json/cluster/resources?type=vm",
             item->Extra1);

    DBGF("[Proxmox]: ProxMox fetching URL: %s\n", url);

    bool success = false;

    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        HTTPClient https;
        https.setTimeout(5000);
        https.begin(client, url);

        // Extra2 = token
        https.addHeader("Authorization", item->Extra2);

        int code = https.GET();

        if (code <= 0) {
            DBGF("[Proxmox]: Attempt %d: ProxMox GET failed, code: %d", attempt, code);
            https.end();
            delay(500);
            continue;
        }

        if (code != 200) {
            DBGF("[Proxmox]: Attempt %d: HTTP error %d", attempt, code);
            https.end();
            delay(500);
            continue;
        }

        StaticJsonDocument<8192> doc;
        DeserializationError error = deserializeJson(doc, https.getStream());
        https.end();

        if (error) {
            DBGF("[Proxmox]: Attempt %d: JSON parse error: %s", attempt, error.c_str());
            delay(500);
            continue;
        }

        JsonArray vms = doc["data"].as<JsonArray>();

        for (JsonObject vm : vms) {
            const char* name = vm["name"];
            const char* statusStr = vm["status"];

            uint32_t status =
                (strcmp(statusStr, "running") == 0)
                ? 0xF0E1E
                : 0xF0026;

            hostsInfo.push_back(ProxmoxHost(name, status));
            proxmoxNodesCount++;
        }

        DBGF("[Proxmox]: ProxMox total hosts count: %d", proxmoxNodesCount);
        success = true;
        break;
    }

    if (!success) {
        DBGF("[Proxmox]: Failed to fetch ProxMox after %d attempts", maxRetries);
    }

    DBG(F("[Proxmox]: ProxMox: Fetching Done") );
}

void proxmoxWidget(LayoutItem* item)
{
    int col = 0;
    int row = 0;

    for (size_t i = 0; i < hostsInfo.size(); i++) {

        drawSparseString(
            &epaperFont,
            item->PosX + item->ColWidth * col,
            item->PosY + item->RowHeight * row + 8,
            hostsInfo[i].name.c_str(),
            (hostsInfo[i].status == 0xF0026 ? GxEPD_BLACK : ALERT_COLOR)
        );

        drawSparseChar(
            &MDI_22_Sparse,
            item->PosX + item->ColWidth * (col + 1) - 35,
            item->PosY + item->RowHeight * row + 10,
            hostsInfo[i].status,
            (hostsInfo[i].status == 0xF0026 ? GxEPD_BLACK : ALERT_COLOR)
        );

        col++;
        if (col == 2) {
            col = 0;
            row++;
        }
    }
}