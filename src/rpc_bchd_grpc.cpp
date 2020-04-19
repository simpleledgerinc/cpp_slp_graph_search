#include <vector>
#include <string>
#include <memory>
#include <cassert>
#include <iostream>

#include <httplib/httplib.h>
#include <nlohmann/json.hpp>
#include <gs++/util.hpp>
#include <gs++/bhash.hpp>
#include <gs++/rpc.hpp>
#include <gs++/rpc_bchd_grpc.hpp>
#include <libbase64.h>

#include <grpc++/grpc++.h>
#include <bchrpc.grpc.pb.h>


namespace gs {


BchGrpcClient::BchGrpcClient(std::shared_ptr<grpc::Channel> channel)
: stub_(pb::bchrpc::NewStub(channel)) 
{}

std::pair<bool, gs::blockhash> BchGrpcClient::get_block_hash(
    const std::size_t height
) {
    std::cout << "get_block_hash start" << std::endl;

    pb::GetBlockInfoRequest request;
    request.set_height(height);
    std::cout << std::to_string(height) << std::endl;

    pb::GetBlockInfoResponse reply;

    grpc::ClientContext context;
    grpc::Status status = stub_->GetBlockInfo(&context, request, &reply);

    if (! status.ok()) {
        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
        return { false, {} };
    }

    if (! reply.has_info()) {
        std::cout << "grpc client error: block_hash returned no info for ${height}" << std::endl;
        return { false, {} };
    }
    
    std::string s = reply.info().hash();
    gs::blockhash hash(s);
    
    std::cout << "get_block_hash end" << std::endl;
    return { true, hash };       
}

std::pair<bool, std::vector<std::uint8_t>> BchGrpcClient::get_raw_block(
    const gs::blockhash& block_hash
) {
    std::cout << "get_raw_block start" << std::endl;

    pb::GetRawBlockRequest request;
    
    std::string s = std::string(block_hash.v.begin(), block_hash.v.end());
    
    request.set_hash(s);
    
    pb::GetRawBlockResponse reply;
    
    grpc::ClientContext context;
    
    grpc::Status status = stub_->GetRawBlock(&context, request, &reply);
    
    if (! status.ok()) {
        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
        return { false, {} };
    }

    std::string n = reply.block();

    std::vector<uint8_t> block(n.begin(), n.end());
    
    std::cout << "get_raw_block end" << std::endl;
    return { true, block };
}

std::pair<bool, std::uint32_t> BchGrpcClient::get_best_block_height()
{
    std::cout << "get_best_block_height start" << std::endl;

    pb::GetBlockchainInfoRequest request;

    pb::GetBlockchainInfoResponse reply;

    grpc::ClientContext context;
    grpc::Status status = stub_->GetBlockchainInfo(&context, request, &reply);

    if (! status.ok()) {
        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
        return { false, {} };
    }

    std::cout << "get_best_block_height end" << std::endl;
    return { true, reply.best_height() };
}

// FIXME:  bchd has no such method, we would need to manually map 
//          fields from RawTransactoinResponse to json object
// std::pair<bool, nlohmann::json> get_decode_raw_transaction(
// const std::string& hex_str
// ) {
// ... 
// }

std::pair<bool, std::vector<gs::txid>> BchGrpcClient::get_raw_mempool()
{
    std::cout << "get_raw_mempool start" << std::endl;

    pb::GetMempoolRequest request;
    request.set_full_transactions(false);

    pb::GetMempoolResponse reply;

    grpc::ClientContext context;
    grpc::Status status = stub_->GetMempool(&context, request, &reply);

    if (! status.ok()) {
        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
        return { false, {} };
    }

    auto txns = reply.transaction_data();
    std::size_t len = 0;
    std::vector<gs::txid> txids;

    for (auto txn : txns) {
        txids[len] = txn.transaction_hash();
        len++;
    }
    std::cout << "get_raw_mempool end" << std::endl;

    return { true, txids };
}

std::pair<bool, std::vector<std::uint8_t>> BchGrpcClient::get_raw_transaction(
    const gs::txid& txid
) {
    std::cout << "get_raw_transaction start" << std::endl;

    pb::GetRawTransactionRequest request;
    std::string s = std::string(txid.v.begin(), txid.v.end());
    request.set_hash(s);

    pb::GetRawTransactionResponse reply;

    grpc::ClientContext context;

    grpc::Status status = stub_->GetRawTransaction(&context, request, &reply);

    if (! status.ok()) {
        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
        return { false, {} };
    }

    std::string n = reply.transaction();
    std::vector<uint8_t> txn(n.begin(), n.end());

    std::cout << "get_raw_transaction end" << std::endl;
    return { true, txn };
}


}