#include "tamdb/service/grpc_server.h"
#include "tamdb/wal/wal.h"

#include <grpcpp/server_builder.h>

namespace tamdb {

void GRPCServer::start(const std::string& address) {
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());

    HNSWIndexPtr hnswIndexPtr = std::make_shared<HNSWIndex>(16, 200, 128);
    
    WriteAheadLoggerPtr walPtr = std::make_shared<WriteAheadLogger>("./tamdb_wal");
    walPtr->replay(hnswIndexPtr);

    _indexServicePtr = std::make_unique<IndexService>(hnswIndexPtr, walPtr);
    _searchServicePtr = std::make_unique<SearchService>(hnswIndexPtr);

    _tamDBServiceAdapter = std::make_unique<TamDBServiceImpl>(*_indexServicePtr, *_searchServicePtr);
    builder.RegisterService(_tamDBServiceAdapter.get());

    _server = builder.BuildAndStart();
}

void GRPCServer::shutdown() {
    if(_server){
        _server->Shutdown();
    }
        
};
}

