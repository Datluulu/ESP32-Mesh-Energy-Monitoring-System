#ifndef MESH_H
#define MESH_H

#include "esp_err.h"
#include "esp_mesh.h"

// Hàm để lấy BSSID của router trong mạng mesh
esp_err_t esp_mesh_get_router_bssid(uint8_t *router_bssid);
void start_mesh();
#endif // MESH_H
