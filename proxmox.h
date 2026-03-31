#pragma once
#include "app.h"
#include "fetchAllInfo.h"

struct Proxmox {
  const char* host;
  const char* apiKey;
};

struct ProxmoxHost {
  String name;
  uint32_t status;

  // Parameterized constructor
  ProxmoxHost() : name(""), status(0) {}

  // Parameterized constructor
  ProxmoxHost(const String& n, uint32_t s) : name(n), status(s) {}
};

void fetchProxmoxStates(LayoutItem* , int );
void proxmoxWidget(LayoutItem* );
