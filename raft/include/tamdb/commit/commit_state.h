#pragma once

#include <vector>
#include <cstdint>

namespace tamdb {
/**
 * Abstract interface for applying committed Raft log entries.
 * Implementations handle how committed data is persisted to the engine.
 * Lives in raft/ to keep the consensus layer engine-agnostic.
 */
class ICommitState {
public: 
    /**
     * Apply a committed entry to the underlying engine.
     *
     * @param id           External vector ID.
     * @param input_vector Vector data to insert.
     * @return True if the entry was applied successfully.
     */
    virtual bool commit(uint64_t id, const std::vector<float>& input_vector) = 0;

    /** Virtual destructor for proper cleanup through base pointer. */
    ~ICommitState() = default;
};
}
