#include "concurrency/lock_manager.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <thread>

#include "common/rowid.h"
#include "concurrency/txn.h"
#include "concurrency/txn_manager.h"

void LockManager::SetTxnMgr(TxnManager *txn_mgr) { txn_mgr_ = txn_mgr; }

namespace {

void AbortTxn(Txn *txn, AbortReason reason) {
    txn->SetState(TxnState::kAborted);
    throw TxnAbortException(txn->GetTxnId(), reason);
}

bool IsWaiting(const LockManager::LockRequest &request) { return request.lock_mode_ != request.granted_; }

}  // namespace

bool LockManager::LockShared(Txn *txn, const RowId &rid) {
    if (txn->GetExclusiveLockSet().count(rid) != 0 || txn->GetSharedLockSet().count(rid) != 0) {
        return true;
    }
    if (txn->GetIsolationLevel() == IsolationLevel::kReadUncommitted) {
        AbortTxn(txn, AbortReason::kLockSharedOnReadUncommitted);
    }

    std::unique_lock<std::mutex> lock(latch_);
    LockPrepare(txn, rid);
    auto &req_queue = lock_table_[rid];
    req_queue.EmplaceLockRequest(txn->GetTxnId(), LockMode::kShared);

    while (true) {
        CheckAbort(txn, req_queue);
        if (!req_queue.is_writing_ && !req_queue.is_upgrading_) {
            auto iter = req_queue.GetLockRequestIter(txn->GetTxnId());
            iter->granted_ = LockMode::kShared;
            req_queue.sharing_cnt_++;
            txn->GetSharedLockSet().emplace(rid);
            return true;
        }
        req_queue.cv_.wait(lock);
    }
}

bool LockManager::LockExclusive(Txn *txn, const RowId &rid) {
    if (txn->GetExclusiveLockSet().count(rid) != 0) {
        return true;
    }
    if (txn->GetSharedLockSet().count(rid) != 0) {
        return LockUpgrade(txn, rid);
    }

    std::unique_lock<std::mutex> lock(latch_);
    LockPrepare(txn, rid);
    auto &req_queue = lock_table_[rid];
    req_queue.EmplaceLockRequest(txn->GetTxnId(), LockMode::kExclusive);

    while (true) {
        CheckAbort(txn, req_queue);
        if (!req_queue.is_writing_ && req_queue.sharing_cnt_ == 0) {
            auto iter = req_queue.GetLockRequestIter(txn->GetTxnId());
            iter->granted_ = LockMode::kExclusive;
            req_queue.is_writing_ = true;
            txn->GetExclusiveLockSet().emplace(rid);
            return true;
        }
        req_queue.cv_.wait(lock);
    }
}

bool LockManager::LockUpgrade(Txn *txn, const RowId &rid) {
    if (txn->GetExclusiveLockSet().count(rid) != 0) {
        return true;
    }

    std::unique_lock<std::mutex> lock(latch_);
    LockPrepare(txn, rid);
    if (txn->GetSharedLockSet().count(rid) == 0) {
        return false;
    }

    auto &req_queue = lock_table_[rid];
    auto iter = req_queue.GetLockRequestIter(txn->GetTxnId());
    if (req_queue.is_upgrading_ && !(iter->lock_mode_ == LockMode::kExclusive && iter->granted_ == LockMode::kShared)) {
        AbortTxn(txn, AbortReason::kUpgradeConflict);
    }

    req_queue.is_upgrading_ = true;
    iter->lock_mode_ = LockMode::kExclusive;

    while (true) {
        CheckAbort(txn, req_queue);
        if (!req_queue.is_writing_ && req_queue.sharing_cnt_ == 1) {
            iter = req_queue.GetLockRequestIter(txn->GetTxnId());
            iter->granted_ = LockMode::kExclusive;
            req_queue.sharing_cnt_--;
            req_queue.is_writing_ = true;
            req_queue.is_upgrading_ = false;
            txn->GetSharedLockSet().erase(rid);
            txn->GetExclusiveLockSet().emplace(rid);
            req_queue.cv_.notify_all();
            return true;
        }
        req_queue.cv_.wait(lock);
    }
}

bool LockManager::Unlock(Txn *txn, const RowId &rid) {
    std::unique_lock<std::mutex> lock(latch_);
    auto queue_iter = lock_table_.find(rid);
    if (queue_iter == lock_table_.end()) {
        return false;
    }

    auto &req_queue = queue_iter->second;
    auto req_iter = req_queue.req_list_iter_map_.find(txn->GetTxnId());
    if (req_iter == req_queue.req_list_iter_map_.end()) {
        return false;
    }

    auto &request = *req_iter->second;
    if (request.granted_ == LockMode::kShared) {
        req_queue.sharing_cnt_--;
        txn->GetSharedLockSet().erase(rid);
    } else if (request.granted_ == LockMode::kExclusive) {
        req_queue.is_writing_ = false;
        txn->GetExclusiveLockSet().erase(rid);
    }
    if (request.lock_mode_ == LockMode::kExclusive && request.granted_ == LockMode::kShared) {
        req_queue.is_upgrading_ = false;
    }

    req_queue.EraseLockRequest(txn->GetTxnId());
    if (txn->GetState() == TxnState::kGrowing) {
        txn->SetState(TxnState::kShrinking);
    }

    req_queue.cv_.notify_all();
    return true;
}

void LockManager::LockPrepare(Txn *txn, const RowId &rid) {
    (void)rid;
    if (txn->GetState() == TxnState::kShrinking) {
        AbortTxn(txn, AbortReason::kLockOnShrinking);
    }
    if (txn->GetState() == TxnState::kAborted) {
        throw TxnAbortException(txn->GetTxnId(), AbortReason::kDeadlock);
    }
}

void LockManager::CheckAbort(Txn *txn, LockManager::LockRequestQueue &req_queue) {
    if (txn->GetState() != TxnState::kAborted) {
        return;
    }

    auto iter = req_queue.req_list_iter_map_.find(txn->GetTxnId());
    if (iter != req_queue.req_list_iter_map_.end()) {
        auto &request = *iter->second;
        if (request.granted_ == LockMode::kShared) {
            req_queue.sharing_cnt_--;
            for (auto &entry : lock_table_) {
                if (&entry.second == &req_queue) {
                    txn->GetSharedLockSet().erase(entry.first);
                    break;
                }
            }
        } else if (request.granted_ == LockMode::kExclusive) {
            req_queue.is_writing_ = false;
            for (auto &entry : lock_table_) {
                if (&entry.second == &req_queue) {
                    txn->GetExclusiveLockSet().erase(entry.first);
                    break;
                }
            }
        }
        if (request.lock_mode_ == LockMode::kExclusive && request.granted_ == LockMode::kShared) {
            req_queue.is_upgrading_ = false;
        }
        req_queue.EraseLockRequest(txn->GetTxnId());
        req_queue.cv_.notify_all();
    }
    throw TxnAbortException(txn->GetTxnId(), AbortReason::kDeadlock);
}

void LockManager::AddEdge(txn_id_t t1, txn_id_t t2) {
    std::unique_lock<std::mutex> lock(latch_);
    waits_for_[t1].emplace(t2);
}

void LockManager::RemoveEdge(txn_id_t t1, txn_id_t t2) {
    std::unique_lock<std::mutex> lock(latch_);
    auto iter = waits_for_.find(t1);
    if (iter == waits_for_.end()) {
        return;
    }
    iter->second.erase(t2);
    if (iter->second.empty()) {
        waits_for_.erase(iter);
    }
}

bool LockManager::HasCycle(txn_id_t &newest_tid_in_cycle) {
    visited_set_.clear();
    revisited_node_ = INVALID_TXN_ID;
    while (!visited_path_.empty()) {
        visited_path_.pop();
    }

    std::vector<txn_id_t> nodes;
    nodes.reserve(waits_for_.size());
    for (const auto &entry : waits_for_) {
        nodes.emplace_back(entry.first);
        for (auto next : entry.second) {
            nodes.emplace_back(next);
        }
    }
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());

    std::unordered_set<txn_id_t> finished;
    std::vector<txn_id_t> path;
    std::unordered_map<txn_id_t, size_t> path_pos;

    std::function<bool(txn_id_t)> dfs = [&](txn_id_t node) -> bool {
        visited_set_.emplace(node);
        path_pos[node] = path.size();
        path.emplace_back(node);

        auto iter = waits_for_.find(node);
        if (iter != waits_for_.end()) {
            for (auto next : iter->second) {
                if (path_pos.count(next) != 0) {
                    newest_tid_in_cycle = next;
                    for (size_t i = path_pos[next]; i < path.size(); i++) {
                        newest_tid_in_cycle = std::max(newest_tid_in_cycle, path[i]);
                    }
                    revisited_node_ = next;
                    return true;
                }
                if (finished.count(next) == 0 && dfs(next)) {
                    return true;
                }
            }
        }

        path_pos.erase(node);
        path.pop_back();
        finished.emplace(node);
        return false;
    };

    newest_tid_in_cycle = INVALID_TXN_ID;
    for (auto node : nodes) {
        if (finished.count(node) == 0 && dfs(node)) {
            return true;
        }
    }
    return false;
}

void LockManager::DeleteNode(txn_id_t txn_id) {
    waits_for_.erase(txn_id);
    for (auto iter = waits_for_.begin(); iter != waits_for_.end();) {
        iter->second.erase(txn_id);
        if (iter->second.empty()) {
            iter = waits_for_.erase(iter);
        } else {
            ++iter;
        }
    }
}

void LockManager::RunCycleDetection() {
    while (enable_cycle_detection_) {
        std::this_thread::sleep_for(cycle_detection_interval_);

        std::unique_lock<std::mutex> lock(latch_);
        waits_for_.clear();
        for (const auto &entry : lock_table_) {
            const auto &req_queue = entry.second;
            for (const auto &waiting_req : req_queue.req_list_) {
                if (!IsWaiting(waiting_req)) {
                    continue;
                }
                for (const auto &granted_req : req_queue.req_list_) {
                    if (granted_req.granted_ == LockMode::kNone || granted_req.txn_id_ == waiting_req.txn_id_) {
                        continue;
                    }
                    if (waiting_req.lock_mode_ == LockMode::kShared && granted_req.granted_ != LockMode::kExclusive) {
                        continue;
                    }
                    waits_for_[waiting_req.txn_id_].emplace(granted_req.txn_id_);
                }
            }
        }

        txn_id_t victim = INVALID_TXN_ID;
        if (HasCycle(victim) && txn_mgr_ != nullptr) {
            auto *txn = txn_mgr_->GetTransaction(victim);
            if (txn != nullptr) {
                txn->SetState(TxnState::kAborted);
            }
            DeleteNode(victim);
            for (auto &entry : lock_table_) {
                entry.second.cv_.notify_all();
            }
        }
    }
}

std::vector<std::pair<txn_id_t, txn_id_t>> LockManager::GetEdgeList() {
    std::unique_lock<std::mutex> lock(latch_);
    std::vector<std::pair<txn_id_t, txn_id_t>> result;
    for (const auto &entry : waits_for_) {
        for (auto to : entry.second) {
            result.emplace_back(entry.first, to);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}
