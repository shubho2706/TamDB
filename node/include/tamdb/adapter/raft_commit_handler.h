#pragma once

#include "tamdb/commit/commit_state.h"
#include "tamdb/service/index_service.h"

namespace tamdb {

class CommitStateHandler : public ICommitState {
public: 
    
    /**
     * Constructs the handler with a reference to the IndexService.
     *
     * @param index_service Reference to the shared IndexService for applying inserts.
     */
    CommitStateHandler(IndexService& index_service);

    /**
     * Apply a committed Raft entry by inserting the vector into the HNSW index.
     *
     * @param id           External vector ID.
     * @param input_vector Vector data to insert.
     * @return True if the insert succeeded.
     */
    bool commit(uint64_t id, const std::vector<float>& input_vector) override;

private:
    IndexService& _index_service;
};
}
