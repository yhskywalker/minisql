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
        for (const auto &entry : log_recs_) {
            auto &log_rec = entry.second;
            if (log_rec->lsn_ <= persist_lsn_) {
                continue;
            }
            switch (log_rec->type_) {
                case LogRecType::kBegin:
                    active_txns_[log_rec->txn_id_] = log_rec->lsn_;
                    break;
                case LogRecType::kInsert:
                    RedoLog(log_rec);
                    active_txns_[log_rec->txn_id_] = log_rec->lsn_;
                    break;
                case LogRecType::kDelete:
                    RedoLog(log_rec);
                    active_txns_[log_rec->txn_id_] = log_rec->lsn_;
                    break;
                case LogRecType::kUpdate:
                    RedoLog(log_rec);
                    active_txns_[log_rec->txn_id_] = log_rec->lsn_;
                    break;
                case LogRecType::kCommit:
                    active_txns_.erase(log_rec->txn_id_);
                    break;
                case LogRecType::kAbort:
                    UndoTxn(log_rec->prev_lsn_);
                    active_txns_.erase(log_rec->txn_id_);
                    break;
                default:
                    break;
            }
        }
    }

    void UndoPhase() {
        for (auto iter = active_txns_.begin(); iter != active_txns_.end(); ++iter) {
            UndoTxn(iter->second);
        }
        active_txns_.clear();
    }

    // used for test only
    void AppendLogRec(LogRecPtr log_rec) { log_recs_.emplace(log_rec->lsn_, log_rec); }

    // used for test only
    inline KvDatabase &GetDatabase() { return data_; }

private:
    void RedoLog(const LogRecPtr &log_rec) {
        switch (log_rec->type_) {
            case LogRecType::kInsert:
                data_[log_rec->new_key_] = log_rec->new_val_;
                break;
            case LogRecType::kDelete:
                data_.erase(log_rec->old_key_);
                break;
            case LogRecType::kUpdate:
                if (log_rec->new_key_ != log_rec->old_key_) {
                    data_.erase(log_rec->old_key_);
                }
                data_[log_rec->new_key_] = log_rec->new_val_;
                break;
            default:
                break;
        }
    }

    void UndoLog(const LogRecPtr &log_rec) {
        switch (log_rec->type_) {
            case LogRecType::kInsert:
                data_.erase(log_rec->new_key_);
                break;
            case LogRecType::kDelete:
                data_[log_rec->old_key_] = log_rec->old_val_;
                break;
            case LogRecType::kUpdate:
                if (log_rec->new_key_ != log_rec->old_key_) {
                    data_.erase(log_rec->new_key_);
                }
                data_[log_rec->old_key_] = log_rec->old_val_;
                break;
            default:
                break;
        }
    }

    void UndoTxn(lsn_t last_lsn) {
        lsn_t next_lsn = last_lsn;
        while (next_lsn != INVALID_LSN) {
            auto log_iter = log_recs_.find(next_lsn);
            if (log_iter == log_recs_.end()) {
                break;
            }
            auto &log_rec = log_iter->second;
            UndoLog(log_rec);
            next_lsn = log_rec->prev_lsn_;
        }
    }

    std::map<lsn_t, LogRecPtr> log_recs_{};
    lsn_t persist_lsn_{INVALID_LSN};
    ATT active_txns_{};
    KvDatabase data_{};  // all data in database
};

#endif  // MINISQL_RECOVERY_MANAGER_H
