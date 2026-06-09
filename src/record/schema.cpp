#include "record/schema.h"

uint32_t Schema::SerializeTo(char *buf) const {
  char *start = buf;
  // magic number
  MACH_WRITE_UINT32(buf, SCHEMA_MAGIC_NUM);
  buf += 4;
  // column count
  uint32_t column_count = static_cast<uint32_t>(columns_.size());
  MACH_WRITE_UINT32(buf, column_count);
  buf += 4;
  // serialize each column
  for (auto column : columns_) {
    buf += column->SerializeTo(buf);
  }
  return buf - start;
}

uint32_t Schema::GetSerializedSize() const {
  uint32_t size = 4 + 4;  // magic number + column count
  for (auto column : columns_) {
    size += column->GetSerializedSize();
  }
  return size;
}

uint32_t Schema::DeserializeFrom(char *buf, Schema *&schema) {
  char *start = buf;
  // magic number
  uint32_t magic_num = MACH_READ_UINT32(buf);
  buf += 4;
  ASSERT(magic_num == SCHEMA_MAGIC_NUM, "Failed to deserialize schema.");
  // column count
  uint32_t column_count = MACH_READ_UINT32(buf);
  buf += 4;
  // deserialize each column
  std::vector<Column *> columns;
  for (uint32_t i = 0; i < column_count; i++) {
    Column *column = nullptr;
    buf += Column::DeserializeFrom(buf, column);
    columns.push_back(column);
  }
  schema = new Schema(columns, true);
  return buf - start;
}