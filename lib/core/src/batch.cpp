#include "batch.h"

#include "record.h"

namespace core {

int committedRecords(size_t fileSize, size_t batchStart, int pending) {
  if (fileSize <= batchStart) return 0;
  const size_t n = (fileSize - batchStart) / kRecordLen;
  return n > static_cast<size_t>(pending) ? pending : static_cast<int>(n);
}

}  // namespace core
