#pragma once
#include "app.h"
#include "fetchAllInfo.h"

struct Proxmox {
  const char* host;
  const char* apiKey;
};

struct ProxmoxHost {
  char name[CFG_PROXMOX_NAME_MAX];
  uint32_t status;

  ProxmoxHost() : status(0) { name[0] = '\0'; }

  ProxmoxHost(const char* n, uint32_t s) : status(s) {
    if (!n) n = "";
    strncpy(name, n, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
  }
};

void fetchProxmoxStates(LayoutItem* , int );
void proxmoxWidget(LayoutItem* );
