#include "record/schema.h"

/**
 * TODO: Student Implement
 */
uint32_t Schema::SerializeTo(char *buf) const {
  uint32_t ofs = 0;
  MACH_WRITE_UINT32(buf + ofs, SCHEMA_MAGIC_NUM);
  ofs += sizeof(uint32_t);
  MACH_WRITE_UINT32(buf + ofs, static_cast<uint32_t>(columns_.size()));
  ofs += sizeof(uint32_t);
  for (auto column : columns_) {
    ofs += column->SerializeTo(buf + ofs);
  }
  return ofs;
}

uint32_t Schema::GetSerializedSize() const {
  uint32_t size = 2 * sizeof(uint32_t);
  for (auto column : columns_) {
    size += column->GetSerializedSize();
  }
  return size;
}

uint32_t Schema::DeserializeFrom(char *buf, Schema *&schema) {
  uint32_t ofs = 0;
  uint32_t magic_num = MACH_READ_UINT32(buf + ofs);
  ofs += sizeof(uint32_t);
  ASSERT(magic_num == SCHEMA_MAGIC_NUM, "Failed to deserialize schema: invalid magic number.");

  uint32_t column_count = MACH_READ_UINT32(buf + ofs);
  ofs += sizeof(uint32_t);
  std::vector<Column *> columns;
  columns.reserve(column_count);
  for (uint32_t i = 0; i < column_count; i++) {
    Column *column = nullptr;
    ofs += Column::DeserializeFrom(buf + ofs, column);
    columns.emplace_back(column);
  }
  schema = new Schema(columns, true);
  return ofs;
}
