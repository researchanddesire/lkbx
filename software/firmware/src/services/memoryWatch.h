#ifndef LOCKBOX_MEMORY_WATCH_H
#define LOCKBOX_MEMORY_WATCH_H

#include <stdint.h>

// Internal RAM is what the box runs short of. Ordinary small allocations move
// to PSRAM when it is gone, but what must be internal fails: a task's stack,
// the two 1.6 KB blocks the encryption hardware needs for every TLS record
// (`esp-aes: Failed to allocate memory`), and whatever the Wi-Fi and
// Bluetooth drivers ask for by that name. On the 2026-11-17 rig run on R8
// such failures were followed by 95 and 110 seconds cut off with the station
// up: the MQTT session dropped, and every new connection timed out until
// network recovery connected the station again (utils/networkRecovery.h).
//
// The heap calls a hook whenever an allocation fails. This keeps when that
// last happened to an allocation that had to be internal, so network recovery
// can look into a lost broker sooner when memory was the likely cause, and
// counts them for the log.

// Registers the hook. Once, at boot.
void initMemoryWatch();

// An allocation that had to come from internal RAM failed within the last
// `withinMs`.
bool internalMemoryWasShort(uint32_t withinMs);

// How many such allocations have failed since the box started.
uint32_t internalAllocationFailures();

// Called often from the activity log task. Once a minute the memory picture
// goes to the log: the largest free block of internal RAM, how much is free,
// the lowest it has been and how many allocations have failed. A box that
// loses memory over hours shows it here long before anything fails.
void serviceMemoryWatch();

#endif  // LOCKBOX_MEMORY_WATCH_H
