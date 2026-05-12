#pragma once

#include "fs_indexed_store.h"

#include <ESPAsyncWebServer.h>

// Register standard CRUD REST endpoints for a FsIndexedStore instance.
//
// Registers:
//   GET    {base_url}       — returns manifest JSON (list, fast)
//   GET    {base_url}/{id}  — streams full document from flash (AsyncFileResponse)
//   DELETE {base_url}/{id}  — deletes document and updates manifest
//   PATCH  {base_url}/{id}  — patches metadata fields in manifest + data file
//
// POST is intentionally excluded: document creation is feature-specific
// (firmware writes documents; the portal does not create raw documents).
//
// Callers add any feature-specific endpoints (e.g. POST) alongside:
//
//   void register_shutter_session_routes(AsyncWebServer& server) {
//       fs_indexed_store_register_routes(server, s_sessions, "/api/shutter/sessions");
//       server.on("/api/shutter/sessions", HTTP_POST, handlePostSession, nullptr, handlePostSessionBody);
//   }
void fs_indexed_store_register_routes(AsyncWebServer& server,
                                      FsIndexedStore& store,
                                      const char* base_url);
