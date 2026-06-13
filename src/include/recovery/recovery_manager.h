#ifndef MINISQL_RECOVERY_MANAGER_H
#define MINISQL_RECOVERY_MANAGER_H

#include <map>
#include <unordered_map>
#include <vector>

#include "recovery/log_rec.h"

using KvDatabase = std::unordered_map<KeyType, ValType>;
using ATT = std::unordered_map<txn_id_t, lsn_t>;

struct CheckPoint {
    lsn_t checkpoint_lsn_{INVALID_LSN};
    ATT active_txns_{};
    KvDatabase persist_data_{};

    inline void AddActiveTxn(txn_id_t txn_id, lsn_t last_lsn) { active_txns_[txn_id] = last_lsn; }

    inline void AddData(KeyType key, ValType val) { persist_data_.emplace(std::move(key), val); }
};

class RecoveryManager {
public:
    void Init(CheckPoint &last_checkpoint) {
        persist_lsn_ = last_checkpoint.checkpoint_lsn_;
        active_txns_ = last_checkpoint.active_txns_;
        data_ = last_checkpoint.persist_data_;
    }

    void RedoPhase() {
        for (auto &[lsn, log] : log_recs_) {
            if (lsn <= persist_lsn_) continue;
            switch (log->type_) {
                case LogRecType::kBegin:
                    active_txns_[log->txn_id_] = lsn;
                    break;
                case LogRecType::kInsert:
                    data_[log->key_] = log->val_;
                    active_txns_[log->txn_id_] = lsn;
                    break;
                case LogRecType::kDelete:
                    data_.erase(log->key_);
                    active_txns_[log->txn_id_] = lsn;
                    break;
                case LogRecType::kUpdate:
                    data_[log->new_key_] = log->new_val_;
                    active_txns_[log->txn_id_] = lsn;
                    break;
                case LogRecType::kCommit:
                    active_txns_.erase(log->txn_id_);
                    break;
                case LogRecType::kAbort:
                    UndoTxn(log->txn_id_);
                    active_txns_.erase(log->txn_id_);
                    break;
                default:
                    break;
            }
        }
    }

    void UndoPhase() {
        auto txns = active_txns_;  // copy because UndoTxn modifies it
        for (auto &[txn_id, lsn] : txns) {
            UndoTxn(txn_id);
            active_txns_.erase(txn_id);
        }
    }

    // used for test only
    void AppendLogRec(LogRecPtr log_rec) { log_recs_.emplace(log_rec->lsn_, log_rec); }

    // used for test only
    inline KvDatabase &GetDatabase() { return data_; }

private:
    void UndoTxn(txn_id_t txn_id) {
        // Iterate logs in reverse order, undo operations belonging to this txn
        for (auto it = log_recs_.rbegin(); it != log_recs_.rend(); ++it) {
            auto &log = it->second;
            if (log->txn_id_ != txn_id) continue;
            switch (log->type_) {
                case LogRecType::kInsert:
                    // Undo insert = delete
                    data_.erase(log->key_);
                    break;
                case LogRecType::kDelete:
                    // Undo delete = restore old value
                    data_[log->key_] = log->val_;
                    break;
                case LogRecType::kUpdate:
                    // Undo update = restore old value
                    data_[log->old_key_] = log->old_val_;
                    break;
                default:
                    break;
            }
        }
    }

private:
    std::map<lsn_t, LogRecPtr> log_recs_{};
    lsn_t persist_lsn_{INVALID_LSN};
    ATT active_txns_{};
    KvDatabase data_{};  // all data in database
};

#endif  // MINISQL_RECOVERY_MANAGER_H
