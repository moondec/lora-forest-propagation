// Binary log on LittleFS: 32-byte records buffered in RAM and appended in batches.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "record.h"

namespace storage {

// Mounts the log partition. Never formats: a mount failure must not erase undownloaded logs.
bool begin();
// Erases the whole log partition and mounts it again (operator command "format confirm").
bool format();
// Starts a new file /log/L<n>.bin whose first record is the header. *number is the first
// candidate; existing names are skipped and *number returns the one used. False (and the old
// file stays open) if the old file's pending records could not be written first.
bool openNew(uint32_t* number, const core::HeaderRec& h);
// Writes the pending records and closes the current file (before it is downloaded).
bool close();
bool isOpen();
const char* currentName();
// Queues one record in RAM (writes only if the 64-record buffer is full). False if full, closed or dropped.
bool append(const uint8_t rec[core::kRecordLen]);
bool flush();  // false if records are still waiting in RAM (they are retried later)
void service();  // writes when 16 records are pending or every 60 s; call only when no radio deadline is near
bool full();     // less than 64 kB free: logging stopped
size_t freeKb();
uint32_t droppedRecords();  // records lost: flash full or writes kept failing
// "name size" lines, one per file.
void listFiles(void (*emit)(const char* line));
bool erase(const char* name);  // "all" erases every log file except the open one

}  // namespace storage
