#include "record/row.h"

/**
 * TODO: Student Implement
 */
uint32_t Row::SerializeTo(char *buf, Schema *schema) const {
  ASSERT(schema != nullptr, "Invalid schema before serialize.");
  ASSERT(schema->GetColumnCount() == fields_.size(), "Fields size do not match schema's column size.");
  char *start = buf;
  uint32_t field_count = static_cast<uint32_t>(fields_.size());
  // field count
  MACH_WRITE_UINT32(buf, field_count);
  buf += 4;
  // null bitmap: one bit per field, 1 means null
  uint32_t bitmap_size = (field_count + 7) / 8;
  unsigned char *null_bitmap = new unsigned char[bitmap_size]();
  for (uint32_t i = 0; i < field_count; i++) {
    if (fields_[i]->IsNull()) {
      null_bitmap[i / 8] |= (1U << (i % 8));
    }
  }
  memcpy(buf, null_bitmap, bitmap_size);
  buf += bitmap_size;
  delete[] null_bitmap;
  // serialize each non-null field
  for (uint32_t i = 0; i < field_count; i++) {
    if (!fields_[i]->IsNull()) {
      buf += fields_[i]->SerializeTo(buf);
    }
  }
  return buf - start;
}

uint32_t Row::DeserializeFrom(char *buf, Schema *schema) {
  ASSERT(schema != nullptr, "Invalid schema before serialize.");
  ASSERT(fields_.empty(), "Non empty field in row.");
  char *start = buf;
  // field count
  uint32_t field_count = MACH_READ_UINT32(buf);
  buf += 4;
  // null bitmap
  uint32_t bitmap_size = (field_count + 7) / 8;
  char *null_bitmap = buf;
  buf += bitmap_size;
  // deserialize each field according to schema
  const auto &columns = schema->GetColumns();
  for (uint32_t i = 0; i < field_count; i++) {
    bool is_null = (null_bitmap[i / 8] >> (i % 8)) & 1;
    Field *field = nullptr;
    buf += Field::DeserializeFrom(buf, columns[i]->GetType(), &field, is_null);
    fields_.push_back(field);
  }
  return buf - start;
}

uint32_t Row::GetSerializedSize(Schema *schema) const {
  ASSERT(schema != nullptr, "Invalid schema before serialize.");
  ASSERT(schema->GetColumnCount() == fields_.size(), "Fields size do not match schema's column size.");
  uint32_t field_count = static_cast<uint32_t>(fields_.size());
  uint32_t size = 4;  // field count
  size += (field_count + 7) / 8;  // null bitmap
  for (uint32_t i = 0; i < field_count; i++) {
    if (!fields_[i]->IsNull()) {
      size += fields_[i]->GetSerializedSize();
    }
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
