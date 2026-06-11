#include "record/column.h"

#include "glog/logging.h"

Column::Column(std::string column_name, TypeId type, uint32_t index, bool nullable, bool unique)
    : name_(std::move(column_name)), type_(type), table_ind_(index), nullable_(nullable), unique_(unique) {
  ASSERT(type != TypeId::kTypeChar, "Wrong constructor for CHAR type.");
  switch (type) {
    case TypeId::kTypeInt:
      len_ = sizeof(int32_t);
      break;
    case TypeId::kTypeFloat:
      len_ = sizeof(float_t);
      break;
    default:
      ASSERT(false, "Unsupported column type.");
  }
}

Column::Column(std::string column_name, TypeId type, uint32_t length, uint32_t index, bool nullable, bool unique)
    : name_(std::move(column_name)),
      type_(type),
      len_(length),
      table_ind_(index),
      nullable_(nullable),
      unique_(unique) {
  ASSERT(type == TypeId::kTypeChar, "Wrong constructor for non-VARCHAR type.");
}

Column::Column(const Column *other)
    : name_(other->name_),
      type_(other->type_),
      len_(other->len_),
      table_ind_(other->table_ind_),
      nullable_(other->nullable_),
      unique_(other->unique_) {}

/**
* TODO: Student Implement
*/
uint32_t Column::SerializeTo(char *buf) const {
  uint32_t ofs = 0;
  MACH_WRITE_UINT32(buf + ofs, COLUMN_MAGIC_NUM);
  ofs += sizeof(uint32_t);

  MACH_WRITE_UINT32(buf + ofs, static_cast<uint32_t>(name_.size()));
  ofs += sizeof(uint32_t);
  MACH_WRITE_STRING(buf + ofs, name_);
  ofs += static_cast<uint32_t>(name_.size());

  MACH_WRITE_UINT32(buf + ofs, static_cast<uint32_t>(type_));
  ofs += sizeof(uint32_t);
  MACH_WRITE_UINT32(buf + ofs, len_);
  ofs += sizeof(uint32_t);
  MACH_WRITE_UINT32(buf + ofs, table_ind_);
  ofs += sizeof(uint32_t);
  MACH_WRITE_UINT32(buf + ofs, nullable_ ? 1 : 0);
  ofs += sizeof(uint32_t);
  MACH_WRITE_UINT32(buf + ofs, unique_ ? 1 : 0);
  ofs += sizeof(uint32_t);
  return ofs;
}

/**
 * TODO: Student Implement
 */
uint32_t Column::GetSerializedSize() const {
  return sizeof(uint32_t) + MACH_STR_SERIALIZED_SIZE(name_) + 5 * sizeof(uint32_t);
}

/**
 * TODO: Student Implement
 */
uint32_t Column::DeserializeFrom(char *buf, Column *&column) {
  uint32_t ofs = 0;
  uint32_t magic_num = MACH_READ_UINT32(buf + ofs);
  ofs += sizeof(uint32_t);
  ASSERT(magic_num == COLUMN_MAGIC_NUM, "Failed to deserialize column: invalid magic number.");

  uint32_t name_len = MACH_READ_UINT32(buf + ofs);
  ofs += sizeof(uint32_t);
  std::string column_name(buf + ofs, buf + ofs + name_len);
  ofs += name_len;

  auto type = static_cast<TypeId>(MACH_READ_UINT32(buf + ofs));
  ofs += sizeof(uint32_t);
  uint32_t len = MACH_READ_UINT32(buf + ofs);
  ofs += sizeof(uint32_t);
  uint32_t table_ind = MACH_READ_UINT32(buf + ofs);
  ofs += sizeof(uint32_t);
  bool nullable = MACH_READ_UINT32(buf + ofs) != 0;
  ofs += sizeof(uint32_t);
  bool unique = MACH_READ_UINT32(buf + ofs) != 0;
  ofs += sizeof(uint32_t);

  if (type == TypeId::kTypeChar) {
    column = new Column(column_name, type, len, table_ind, nullable, unique);
  } else {
    column = new Column(column_name, type, table_ind, nullable, unique);
  }
  return ofs;
}
