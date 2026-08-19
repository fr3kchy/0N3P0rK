#pragma once
#include <stddef.h>
#include <FS.h>
class WiFiClientSecure;

// Same idea as M5PORKCHOP: lend the screen buffer to mbedTLS so
// handshake RAM does not come from a shredded heap (that hang).
namespace Tls {
void arenaBegin(void* buf, size_t size);
void arenaEnd();
bool streamFile(WiFiClientSecure& client, File& file, size_t fileSize,
                char* outError, size_t outErrorLen);
}
