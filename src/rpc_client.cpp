#include <gs++/rpc_json.hpp>
#include <gs++/rpc_grpc.hpp>
#include <gs++/rpc_client.hpp>

namespace gs
{

RpcClient::RpcClient()
{}

void RpcClient::set_json_rpc(gs::rpc & rpc) {
    RpcClient::rpc_json = &rpc;
    RpcClient::rpc_grpc = NULL;
}

void RpcClient::set_grpc_rpc(gs::BchdGrpcClient & rpc) {
    RpcClient::rpc_grpc = &rpc;
    RpcClient::rpc_json = NULL;
}

std::pair<bool, gs::blockhash> RpcClient::get_block_hash(const std::size_t height) {
    if (RpcClient::rpc_json) {
        return RpcClient::rpc_json->get_block_hash(height);
    } else {

        return RpcClient::rpc_grpc->get_block_hash(height);
    }
}

std::pair<bool, std::vector<std::uint8_t>> RpcClient::get_raw_block(const gs::blockhash& block_hash) {
    if (RpcClient::rpc_json) {
        return RpcClient::rpc_json->get_raw_block(block_hash);
    } else {
        return RpcClient::rpc_grpc->get_raw_block(block_hash);
    }
}


std::pair<bool, std::uint32_t> RpcClient::get_best_block_height() {
    if (RpcClient::rpc_json) {
        return RpcClient::rpc_json->get_best_block_height();
    } else {
        return RpcClient::rpc_grpc->get_best_block_height();
    }
}

std::pair<bool, std::vector<gs::txid>> RpcClient::get_raw_mempool() {
    if (RpcClient::rpc_json) {
        return RpcClient::rpc_json->get_raw_mempool();
    } else {
        return RpcClient::rpc_grpc->get_raw_mempool();
    }
}

std::pair<bool, std::vector<std::uint8_t>> RpcClient::get_raw_transaction(const gs::txid& txid) {
    if (RpcClient::rpc_json) {
        return RpcClient::rpc_json->get_raw_transaction(txid);
    } else {
        return RpcClient::rpc_grpc->get_raw_transaction(txid);
    }
}   

int RpcClient::subscribe_raw_transactions(std::function<void (std::string txn)> callback) {
    return RpcClient::rpc_grpc->subscribe_raw_transactions(callback);
}

int RpcClient::subscribe_raw_blocks(std::function<void (std::string block)> callback) {
    return RpcClient::rpc_grpc->subscribe_raw_blocks(callback);
}


}

