#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <cstdlib>
#include <cstdint>
#include <csignal>
#include <cassert>
#include <unistd.h>
#include <getopt.h>

#include <absl/container/node_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/flat_hash_map.h>
#include <grpc++/grpc++.h>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/string/to_string.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>


#include "helloworld.grpc.pb.h"


using txhash = std::string;

constexpr std::size_t txid_size = 64;


struct transaction
{
    txhash txid;
    std::string txdata;
    std::vector<txhash> inputs;

    transaction(
        txhash txid,
        std::string txdata,
        std::vector<txhash> inputs
    )
    : txid(txid)
    , txdata(txdata)
    , inputs(inputs)
    {}
};

struct graph_node
{
    txhash                   txid;
    std::vector<graph_node*> inputs;
    std::string              txdata;

    graph_node () {}

    graph_node (txhash txid, std::string txdata)
    : txid(txid)
    , txdata(txdata)
    {}
};

struct token_details
{
    txhash                                  tokenid;
    absl::node_hash_map<txhash, graph_node> graph;

    token_details () {}

    token_details (txhash tokenid)
    : tokenid(tokenid)
    {}
};

absl::node_hash_map<txhash, token_details>  tokens;        // tokenid -> token
absl::node_hash_map<txhash, token_details*> txid_to_token; // txid -> token
std::unique_ptr<grpc::Server> gserver;


void recursive_walk__ptr (
    graph_node* node,
    absl::flat_hash_set<graph_node*> & seen
) {

    for (graph_node* n : node->inputs) {
        if (! seen.count(n)) {
            seen.insert(n);
            recursive_walk__ptr(n, seen);
        }
    }
}

std::vector<std::string> graph_search__ptr(const txhash lookup_txid)
{
    if (txid_to_token.count(lookup_txid) == 0) {
        // txid hasn't entered our system yet
        return {};
    }

    token_details* token = txid_to_token[lookup_txid];

    absl::flat_hash_set<graph_node*> seen;

    if (token->graph.count(lookup_txid) == 0) {
        std::stringstream ss;
        ss << "graph_search__ptr: txid not found in tokengraph " << lookup_txid
           << "\n";
        std::cerr << ss.str();
        return {};
    }
    recursive_walk__ptr(&token->graph[lookup_txid], seen);

    std::vector<std::string> ret;
    ret.reserve(seen.size());

    for (auto it = std::begin(seen); it != std::end(seen); ) {
        ret.emplace_back(std::move(seen.extract(it++).value())->txdata);
    }

    return ret;
}

std::filesystem::path get_tokendir(const txhash tokenid)
{
    std::string p1 = tokenid.substr(0, 1);
    std::string p2 = tokenid.substr(1, 1);
    return std::filesystem::path("cache") / p1 / p2;
}


// TODO save writes into buffer to prevent many tiny writes
// should improve performance
bool save_token_to_disk(const txhash tokenid)
{
    std::cout << "saving token to disk" << tokenid;

    const std::filesystem::path tokendir = get_tokendir(tokenid);
    std::filesystem::create_directories(tokendir);

    const std::filesystem::path tokenpath(tokendir / tokenid);
    std::ofstream outf(tokenpath, std::ofstream::binary);

    for (auto it : tokens[tokenid].graph) {
        auto node = it.second;
        outf.write(node.txid.data(), node.txid.size());

        const std::size_t txdata_size = node.txdata.size();
        outf.write(reinterpret_cast<const char *>(&txdata_size), sizeof(std::size_t));

        outf.write(node.txdata.data(), node.txdata.size());

        const std::size_t inputs_size = node.inputs.size();
        outf.write(reinterpret_cast<const char *>(&inputs_size), sizeof(inputs_size));

        for (graph_node* input : node.inputs) {
            outf.write(input->txid.data(), input->txid.size());
        }
    }

    std::cout << "\t done" << tokenpath << std::endl;

    return true;
}

std::size_t insert_token_data (
    const txhash tokenid,
    std::vector<transaction> txs
) {
    if (tokens.count(tokenid)) {
        tokens.erase(tokenid);
    }

    token_details& token = tokens[tokenid];
    tokens.insert({ tokenid, token_details(tokenid) });

    absl::flat_hash_map<txhash, std::vector<txhash>> input_map;

    std::size_t ret = 0;

    // first pass to populate graph nodes
    for (auto tx : txs) {
        token.graph.insert({ tx.txid, graph_node(tx.txid, tx.txdata) });
        txid_to_token.insert({ tx.txid, &token });
        input_map.insert({ tx.txid, tx.inputs });
        ++ret;
    }

    // second pass to add inputs
    for (auto & it : token.graph) {
        const txhash txid = it.first;
        graph_node & node = it.second;

        for (const txhash input_txid : input_map[txid]) {
            if (! token.graph.count(input_txid)) {
                // std::cerr << "missing input_txid " << input_txid << std::endl;
                continue;
            }

            node.inputs.emplace_back(&token.graph[input_txid]);
        }
    }

    return ret;
}

std::vector<transaction> load_token_from_disk(const txhash tokenid)
{
    std::filesystem::path tokenpath = get_tokendir(tokenid) / tokenid;
    std::ifstream file(tokenpath, std::ios::binary);
    std::cout << "loading token from disk: " << tokenpath << std::endl;
    std::vector<std::uint8_t> fbuf(std::istreambuf_iterator<char>(file), {});
    std::vector<transaction> ret;


    auto it = std::begin(fbuf);
    while (it != std::end(fbuf)) {
        txhash txid(txid_size, '0');
        std::copy(it, it+txid_size, std::begin(txid));
        it += txid_size;

        std::size_t txdata_size;
        std::copy(it, it+sizeof(std::size_t), reinterpret_cast<char*>(&txdata_size));
        it += sizeof(std::size_t);

        std::string txdata(txdata_size, '0');
        std::copy(it, it+txdata_size, std::begin(txdata));
        it += txdata_size;

        std::size_t inputs_size;
        std::copy(it, it+sizeof(inputs_size), reinterpret_cast<char*>(&inputs_size));
        //std::copy(it, it+sizeof(inputs_size), &inputs_size);
        it += sizeof(inputs_size);

        std::vector<txhash> inputs;
        inputs.reserve(inputs_size);
        for (std::size_t i=0; i<inputs_size; ++i) {
            txhash input(txid_size, '0');
            std::copy(it, it+txid_size, std::begin(input));
            it += txid_size;
            inputs.emplace_back(input);
        }

        ret.emplace_back(transaction(txid, txdata, inputs));
    }
    file.close();

    return ret;
}

class GraphSearchServiceServiceImpl final
 : public graphsearch::GraphSearchService::Service
{
    grpc::Status GraphSearch (
        grpc::ServerContext* context,
        const graphsearch::GraphSearchRequest* request,
        graphsearch::GraphSearchReply* reply
    ) override {
        const txhash lookup_txid = request->txid();

        std::stringstream ss;
        ss << "lookup: " << lookup_txid;
        reply->add_txdata();

        const auto start = std::chrono::steady_clock::now();
        std::vector<std::string> result = graph_search__ptr(lookup_txid);
        for (auto i : result) {
            reply->add_txdata(i);
        }
        const auto end = std::chrono::steady_clock::now();
        const auto diff = end - start;

        ss  << "\t" << std::chrono::duration <double, std::milli> (diff).count() << " ms "
            << "(" << result.size() << ")"
            << std::endl;

        std::cout << ss.str();

        return grpc::Status::OK;
    }
};


void signal_handler(int signal)
{
    if (signal == SIGTERM || signal == SIGINT) {
        std::cout << "received signal " << signal << " requesting to shut down" << std::endl;
        gserver->Shutdown();
    }
}


std::vector<txhash> get_all_token_ids(mongocxx::database & db)
{
    auto collection = db["tokens"];

    mongocxx::options::find opts{};
    opts.projection(
        bsoncxx::builder::stream::document{}
    << "tokenDetails.tokenIdHex" << 1
    << bsoncxx::builder::stream::finalize
    );

    std::vector<txhash> ret;
    auto cursor = collection.find({}, opts);
    for (auto&& doc : cursor) {
        const auto el = doc["tokenDetails"]["tokenIdHex"];
        assert(el.type() == bsoncxx::type::k_utf8);
        const std::string tokenIdHexStr = bsoncxx::string::to_string(el.get_utf8().value);
        ret.emplace_back(tokenIdHexStr);
    }

    return ret;
}


std::vector<transaction> load_token_from_mongo (
    mongocxx::database & db,
    const txhash tokenid
) {
    using bsoncxx::builder::basic::make_document;
    using bsoncxx::builder::basic::kvp;

    auto collection = db["graphs"];

    mongocxx::pipeline pipe{};
    pipe.match(make_document(
        kvp("tokenDetails.tokenIdHex", tokenid)
    ));
    pipe.lookup(make_document(
        kvp("from", "confirmed"),
        kvp("localField", "graphTxn.txid"),
        kvp("foreignField", "tx.h"),
        kvp("as", "tx")
    ));
    pipe.project(make_document(
        kvp("graphTxn.txid", 1),
        kvp("graphTxn.inputs.txid", 1),
        kvp("tx.tx.raw", 1)
    ));

    std::vector<transaction> ret;
    auto cursor = collection.aggregate(pipe, mongocxx::options::aggregate{});
    for (auto&& doc : cursor) {
        const auto txid_el = doc["graphTxn"]["txid"];
        assert(txid_el.type() == bsoncxx::type::k_utf8);
        const std::string txidStr = bsoncxx::string::to_string(txid_el.get_utf8().value);

        std::string txdataStr;

        const auto tx_el = doc["tx"];
        const bsoncxx::array::view tx_sarr { tx_el.get_array().value };

        for (bsoncxx::array::element tx_s_el : tx_sarr) {
            auto txdata_el = tx_s_el["tx"]["raw"];
            assert(txdata_el.type() == bsoncxx::type::k_binary);
            auto txdata_bin = txdata_el.get_binary();

            txdataStr.resize(txdata_bin.size, '\0');
            std::copy(
                txdata_bin.bytes,
                txdata_bin.bytes+txdata_bin.size,
                std::begin(txdataStr)
            );

            break; // this is used for $lookup so just 1 item
        }

        std::vector<txhash> inputs;
        const auto inputs_el = doc["graphTxn"]["inputs"];
        const bsoncxx::array::view inputs_sarr { inputs_el.get_array().value };

        for (bsoncxx::array::element input_s_el : inputs_sarr) {
            auto input_txid_el = input_s_el["txid"];
            assert(input_txid_el.type() == bsoncxx::type::k_utf8);
            const std::string input_txidStr = bsoncxx::string::to_string(input_txid_el.get_utf8().value);
            inputs.emplace_back(input_txidStr);
        }

        ret.emplace_back(transaction(txidStr, txdataStr, inputs));
    }

    return ret;
}

int main(int argc, char * argv[])
{
    std::string mongo_db_name = "slpdb";
    std::string grpc_bind = "0.0.0.0";
    std::string grpc_port = "50051";

    while (true) {
        static struct option long_options[] = {
            { "help",    no_argument,       nullptr, 'h' },
            { "version", no_argument,       nullptr, 'v' },
            { "db",      required_argument, nullptr, 'd' },
            { "bind",    required_argument, nullptr, 'b' },
            { "port",    required_argument, nullptr, 'p' },
        };

        int option_index = 0;
        int c = getopt_long(argc, argv, "hvd:b:p:", long_options, &option_index);

        if (c == -1) {
            break;
        }

        switch (c) {
            case 0:
                if (long_options[option_index].flag != 0) {
                    break;
                }

                break;
            case 'h':
                std::cout <<
                    "usage: gs++ [--version] [--help] [--db db_name]\n"
                    "            [--bind bind_address] [--port port]\n";
                return EXIT_SUCCESS;
            case 'v':
                std::cout <<
                    "gs++ v" << GS_VERSION << std::endl;
                return EXIT_SUCCESS;
            case 'd':
                mongo_db_name = optarg;
                break;
            case 'b':
                grpc_bind = optarg;
                break;
            case 'p':
                grpc_port = optarg;
                break;
            case '?':
                return EXIT_FAILURE;
            default:
                return EXIT_FAILURE;
        }
    }


    mongocxx::instance inst{};
    mongocxx::client conn{mongocxx::uri{}};

    bsoncxx::builder::stream::document document{};

    auto db = conn[mongo_db_name];


    try {
        const std::vector<std::string> token_ids = get_all_token_ids(db);

        std::size_t cnt = 0;
        for (auto tokenid : token_ids) {
            std::cout << "loading: " << tokenid;

            auto txs = load_token_from_mongo(db, tokenid);
            const std::size_t txs_inserted = insert_token_data(tokenid, txs);

            ++cnt;
            std::cout
                << "\t" << txs_inserted
                << "\t(" << cnt << "/" << token_ids.size() << ")"
                << std::endl;
        }



    } catch (const std::logic_error& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }


    std::string server_address(grpc_bind+":"+grpc_port);

    GraphSearchServiceServiceImpl graphsearch_service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&graphsearch_service);
    std::unique_ptr<grpc::Server> gserver(builder.BuildAndStart());
    std::cout << "gs++ listening on " << server_address << std::endl;

    gserver->Wait();

    return EXIT_SUCCESS;
}
