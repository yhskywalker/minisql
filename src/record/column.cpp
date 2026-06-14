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

uint32_t Column::SerializeTo(char *buf) const {
  char *start = buf;
  // magic number
  MACH_WRITE_UINT32(buf, COLUMN_MAGIC_NUM);
  buf += 4;
  // column name (length-prefixed string)
  uint32_t name_len = name_.length();
  MACH_WRITE_UINT32(buf, name_len);
  buf += 4;
  MACH_WRITE_STRING(buf, name_);
  buf += name_len;
  // type
  MACH_WRITE_TO(uint32_t, buf, static_cast<uint32_t>(type_));
  buf += 4;
  // length (for CHAR, this is the max byte length; for int/float, it's the fixed size)
  MACH_WRITE_UINT32(buf, len_);
  buf += 4;
  // table index (column position in table)
  MACH_WRITE_UINT32(buf, table_ind_);
  buf += 4;
  // nullable flag
  MACH_WRITE_TO(bool, buf, nullable_);
  buf += 1;
  // unique flag
  MACH_WRITE_TO(bool, buf, unique_);
  buf += 1;
  return buf - start;
}

uint32_t Column::GetSerializedSize() const {
  return 4                          // magic number
       + MACH_STR_SERIALIZED_SIZE(name_)  // 4 + name_.length()
       + 4                          // type_
       + 4                          // len_
       + 4                          // table_ind_
       + 1                          // nullable_
       + 1;                         // unique_
}

/**
 * TODO: Student Implement
 */
uint32_t Column::DeserializeFrom(char *buf, Column *&column) {
  char *start = buf;
  // magic number
  uint32_t magic_num = MACH_READ_UINT32(buf);
  buf += 4;
  ASSERT(magic_num == COLUMN_MAGIC_NUM, "Failed to deserialize column.");
  // column name
  uint32_t name_len = MACH_READ_UINT32(buf);
  buf += 4;
  std::string column_name(buf, name_len);
  buf += name_len;
  // type
  auto type = static_cast<TypeId>(MACH_READ_UINT32(buf));
  buf += 4;
  // length
  uint32_t len = MACH_READ_UINT32(buf);
  buf += 4;
  // table index
  uint32_t table_ind = MACH_READ_UINT32(buf);
  buf += 4;
  // nullable
  bool nullable = MACH_READ_FROM(bool, buf);
  buf += 1;
  // unique
  bool unique = MACH_READ_FROM(bool, buf);
  buf += 1;
  // Use the appropriate constructor: CHAR types need explicit length
  if (type == kTypeChar) {
    column = new Column(column_name, type, len, table_ind, nullable, unique);
  } else {
    column = new Column(column_name, type, table_ind, nullable, unique);
  }
  return buf - start;
}
