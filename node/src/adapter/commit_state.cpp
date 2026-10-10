#include  "tamdb/adapter/raft_commit_handler.h"

namespace tamdb {

CommitStateHandler::CommitStateHandler(IndexService& index_service) 
    : _index_service(index_service) {}

bool CommitStateHandler::commit(uint64_t id, const std::vector<float>& input_vector) {
    bool success = _index_service.insertVector(id, input_vector);
    return success;
}
}