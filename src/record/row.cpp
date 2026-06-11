#include "record/row.h"

/**
 * TODO: Student Implement
 */
uint32_t Row::SerializeTo(char *buf, Schema *schema) const {
  ASSERT(schema != nullptr, "Invalid schema before serialize.");
  ASSERT(schema->GetColumnCount() == fields_.size(), "Fields size do not match schema's column size.");
  uint32_t field_count = static_cast<uint32_t>(fields_.size());
  uint32_t bitmap_size = (field_count + 7) / 8;
  uint32_t ofs = 0;
  MACH_WRITE_UINT32(buf + ofs, field_count);
  ofs += sizeof(uint32_t);
  memset(buf + ofs, 0, bitmap_size);
  for (uint32_t i = 0; i < field_count; i++) {
    if (fields_[i]->IsNull()) {
      buf[ofs + i / 8] |= static_cast<char>(1U << (i % 8));
    }
  }
  ofs += bitmap_size;
  for (uint32_t i = 0; i < field_count; i++) {
    if (!fields_[i]->IsNull()) {
      ofs += fields_[i]->SerializeTo(buf + ofs);
    }
  }
  return ofs;
}

uint32_t Row::DeserializeFrom(char *buf, Schema *schema) {
  ASSERT(schema != nullptr, "Invalid schema before serialize.");
  ASSERT(fields_.empty(), "Non empty field in row.");
  uint32_t ofs = 0;
  uint32_t field_count = MACH_READ_UINT32(buf + ofs);
  ofs += sizeof(uint32_t);
  ASSERT(field_count == schema->GetColumnCount(), "Fields size do not match schema's column size.");
  uint32_t bitmap_size = (field_count + 7) / 8;
  char *null_bitmap = buf + ofs;
  ofs += bitmap_size;
  fields_.reserve(field_count);
  for (uint32_t i = 0; i < field_count; i++) {
    bool is_null = (null_bitmap[i / 8] & static_cast<char>(1U << (i % 8))) != 0;
    Field *field = nullptr;
    ofs += Field::DeserializeFrom(buf + ofs, schema->GetColumn(i)->GetType(), &field, is_null);
    fields_.emplace_back(field);
  }
  return ofs;
}

uint32_t Row::GetSerializedSize(Schema *schema) const {
  ASSERT(schema != nullptr, "Invalid schema before serialize.");
  ASSERT(schema->GetColumnCount() == fields_.size(), "Fields size do not match schema's column size.");
  uint32_t size = sizeof(uint32_t) + (static_cast<uint32_t>(fields_.size()) + 7) / 8;
  for (auto field : fields_) {
    size += field->GetSerializedSize();
  }
  return size;
}

void Row::GetKeyFromRow(const Schema *schema, const Schema *key_schema, Row &key_row) {
  auto columns = key_schema->GetColumns();
  std::vector<Field> fields;
  uint32_t idx;
  for (auto column : columns) {
    schema->GetColumnIndex(column->GetName(), idx);
    fields.emplace_back(*this->GetField(idx));
  }
  key_row = Row(fields);
}
