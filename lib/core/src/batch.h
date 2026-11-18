// Resuming a batch append after a failed or short write, without duplicating records.
#pragma once

#include <stddef.h>

namespace core {

// Whole records of a pending batch (started at byte batchStart) already present in a file of
// fileSize bytes; 0..pending. A trailing partial record does not count and gets overwritten.
int committedRecords(size_t fileSize, size_t batchStart, int pending);

}  // namespace core
