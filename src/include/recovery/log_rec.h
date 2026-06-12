#ifndef MINISQL_LOG_REC_H
#define MINISQL_LOG_REC_H

#include <memory>
#include <unordered_map>
#include <utility>

#include "common/config.h"
#include "common/rowid.h"
#include "record/row.h"

enum class LogRecType {
    kInvalid,
    kInsert,
    kDelete,
    kUpdate,
    kBegin,
    kCommit,
    kAbort,
};

// used for testing only
using KeyType = std::string;
using ValType = int32_t;

struct LogRec {
    LogRec() = default;

    LogRecType type_{LogRecType::kInvalid};
    lsn_t lsn_{INVALID_LSN};
    lsn_t prev_lsn_{INVALID_LSN};
    txn_id_t txn_id_{INVALID_TXN_ID};
    KeyType old_key_{};
    ValType old_val_{};
    KeyType new_key_{};
    ValType new_val_{};

    /* used for testing only */
    static std::unordered_map<txn_id_t, lsn_t> prev_lsn_map_;
    static lsn_t next_lsn_;
};

inline std::unordered_map<txn_id_t, lsn_t> LogRec::prev_lsn_map_ = {};
inline lsn_t LogRec::next_lsn_ = 0;

typedef std::shared_ptr<LogRec> LogRecPtr;

static LogRecPtr CreateLog(LogRecType type, txn_id_t txn_id) {
    auto log_rec = std::make_shared<LogRec>();
    log_rec->type_ = type;
    log_rec->txn_id_ = txn_id;
    log_rec->lsn_ = LogRec::next_lsn_++;
    if (type == LogRecType::kBegin) {
        log_rec->prev_lsn_ = INVALID_LSN;
        LogRec::prev_lsn_map_[txn_id] = log_rec->lsn_;
    } else {
        auto iter = LogRec::prev_lsn_map_.find(txn_id);
        log_rec->prev_lsn_ = iter == LogRec::prev_lsn_map_.end() ? INVALID_LSN : iter->second;
        if (type == LogRecType::kCommit || type == LogRecType::kAbort) {
            LogRec::prev_lsn_map_.erase(txn_id);
        } else {
            LogRec::prev_lsn_map_[txn_id] = log_rec->lsn_;
        }
    }
    return log_rec;
}

static LogRecPtr CreateInsertLog(txn_id_t txn_id, KeyType ins_key, ValType ins_val) {
    auto log_rec = CreateLog(LogRecType::kInsert, txn_id);
    log_rec->new_key_ = std::move(ins_key);
    log_rec->new_val_ = ins_val;
    return log_rec;
}

static LogRecPtr CreateDeleteLog(txn_id_t txn_id, KeyType del_key, ValType del_val) {
    auto log_rec = CreateLog(LogRecType::kDelete, txn_id);
    log_rec->old_key_ = std::move(del_key);
    log_rec->old_val_ = del_val;
    return log_rec;
}

static LogRecPtr CreateUpdateLog(txn_id_t txn_id, KeyType old_key, ValType old_val, KeyType new_key, ValType new_val) {
    auto log_rec = CreateLog(LogRecType::kUpdate, txn_id);
    log_rec->old_key_ = std::move(old_key);
    log_rec->old_val_ = old_val;
    log_rec->new_key_ = std::move(new_key);
    log_rec->new_val_ = new_val;
    return log_rec;
}

static LogRecPtr CreateBeginLog(txn_id_t txn_id) {
    return CreateLog(LogRecType::kBegin, txn_id);
}

static LogRecPtr CreateCommitLog(txn_id_t txn_id) {
    return CreateLog(LogRecType::kCommit, txn_id);
}

static LogRecPtr CreateAbortLog(txn_id_t txn_id) {
    return CreateLog(LogRecType::kAbort, txn_id);
}

#endif  // MINISQL_LOG_REC_H
