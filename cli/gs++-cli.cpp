#include <iostream>
#include <memory>
#include <chrono>
#include <regex>
#include <string>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <getopt.h>

#include "graphsearch.grpc.pb.h"
#include "utxo.grpc.pb.h"

#include <grpc++/grpc++.h>
#include <libbase64.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/numeric/int128.h>

#include <gs++/transaction.hpp>
#include <gs++/bhash.hpp>
#include <gs++/scriptpubkey.hpp>
#include <gs++/slp_validator.hpp>
#include <config.h>

#define TIMER(title, code) {\
    auto start = std::chrono::high_resolution_clock::now();\
    code\
    auto end = std::chrono::high_resolution_clock::now();\
    std::cerr\
        << title << ":\t"\
        << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms\n";\
}


class GraphSearchServiceClient
{
public:
    GraphSearchServiceClient(std::shared_ptr<grpc::Channel> channel)
    : stub_(graphsearch::GraphSearchService::NewStub(channel))
    {}

    bool GraphSearch(
        const std::string& txid_str,
        const std::vector<std::string> exclude_txids
    ) {
        {
            static const std::regex txid_regex("^[0-9a-fA-F]{64}$");
            const bool rmatch = std::regex_match(txid_str, txid_regex);
            if (! rmatch) {
                std::cerr << "txid did not match regex\n";
                return false;
            }
        }

        graphsearch::GraphSearchRequest request;

        gs::txid txid(txid_str);
        std::reverse(txid.v.begin(), txid.v.end());
        request.set_txid(txid.decompress());

        for (const std::string exclude_txid_str : exclude_txids) {
            gs::txid txid(exclude_txid_str);
            std::reverse(txid.v.begin(), txid.v.end());
            request.add_exclude_txids(txid.decompress());
        }

        graphsearch::GraphSearchReply reply;

        grpc::ClientContext context;
        grpc::Status status = stub_->GraphSearch(&context, request, &reply);
        if (! status.ok()) {
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            return false;
        }

        for (auto n : reply.txdata()) {
            // 4/3 is recommended size with a bit of a buffer
            std::string b64(n.size()*1.5, '\0');
            std::size_t b64_len = 0;
            base64_encode(n.data(), n.size(), const_cast<char*>(b64.data()), &b64_len, 0);
            b64.resize(b64_len);
            std::cout << b64 << "\n";
        }

        return true;
    }

    bool GraphSearchValidate(const std::string& txid_str)
    {
        {
            static const std::regex txid_regex("^[0-9a-fA-F]{64}$");
            const bool rmatch = std::regex_match(txid_str, txid_regex);
            if (! rmatch) {
                std::cerr << "txid did not match regex\n";
                return false;
            }
        }

        graphsearch::GraphSearchRequest request;

        gs::txid txid(txid_str);
        std::reverse(txid.v.begin(), txid.v.end());

        request.set_txid(txid.decompress());

        graphsearch::GraphSearchReply reply;

        grpc::ClientContext context;
        grpc::Status status = stub_->GraphSearch(&context, request, &reply);


        if (! status.ok()) {
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            return false;
        }

        gs::slp_validator validator;
        TIMER("hydrate", {
            for (auto & n : reply.txdata()) {
                gs::transaction tx;
                if (! tx.hydrate(n.begin(), n.end())) {
                    std::cerr << "ERROR: could not hydrate from txdata\n";
                    continue;
                }

                // std::cout << "token_type:" << tx.slp.token_type << std::endl;

                validator.add_tx(tx);
            }
        });

        TIMER("validate", {
            const bool valid = validator.validate(txid);
            std::cout << txid.decompress(true) << ": " << ((valid) ? "valid" : "invalid") << "\n";
        });

        return true;
    }

    bool Dot(
        const std::string& txid_str,
        const std::vector<std::string> exclude_txids
    ) {
        {
            static const std::regex txid_regex("^[0-9a-fA-F]{64}$");
            const bool rmatch = std::regex_match(txid_str, txid_regex);
            if (! rmatch) {
                std::cerr << "txid did not match regex\n";
                return false;
            }
        }

        graphsearch::GraphSearchRequest request;

        gs::txid txid(txid_str);
        std::reverse(txid.v.begin(), txid.v.end());

        request.set_txid(txid.decompress());

        for (const std::string exclude_txid_str : exclude_txids) {
            gs::txid txid(exclude_txid_str);
            std::reverse(txid.v.begin(), txid.v.end());
            request.add_exclude_txids(txid.decompress());
        }

        graphsearch::GraphSearchReply reply;

        grpc::ClientContext context;
        grpc::Status status = stub_->GraphSearch(&context, request, &reply);


        if (! status.ok()) {
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            return false;
        }

        std::string odot = "digraph G {\n";

        std::vector<gs::transaction> txs;

        TIMER("hydrate", {
            for (auto & n : reply.txdata()) {
                gs::transaction tx;
                if (! tx.hydrate(n.begin(), n.end())) {
                    std::cerr << "ERROR: could not hydrate from txdata\n";
                    continue;
                }

                txs.emplace_back(tx);
            }
        });

        TIMER("toposort", {
            txs = gs::util::topological_sort(txs);
        });

        std::size_t slice_begin = 0;
        std::size_t slice_end = txs.size();

        txs = decltype(txs)(txs.cbegin() + slice_begin, txs.cbegin() + slice_end);

        absl::flat_hash_set<gs::txid> txids;
        for (auto & tx : txs) {
            txids.insert(tx.txid);
            for (auto & m : tx.inputs) {
                txids.insert(m.txid);
            }
        }

        // don't draw labels
        for (auto & txid : txids) {
            odot += "t" + txid.decompress(true) + " [label=\"\"];\n";
        }

        for (auto & tx : txs) {
            for (auto & m : tx.inputs) {
                odot += "t" + m.txid.decompress(true) + " -> t" + tx.txid.decompress(true) + ";\n";
            }
        }
        odot += "}\n";

        std::cout << odot;

        return true;
    }

    bool GraphSearchTrustedValidate(const std::string& txid_str)
    {
        {
            static const std::regex txid_regex("^[0-9a-fA-F]{64}$");
            const bool rmatch = std::regex_match(txid_str, txid_regex);
            if (! rmatch) {
                std::cerr << "txid did not match regex\n";
                return false;
            }
        }

        graphsearch::TrustedValidationRequest request;

        gs::txid txid(txid_str);
        std::reverse(txid.v.begin(), txid.v.end());

        request.set_txid(txid.decompress());

        graphsearch::TrustedValidationReply reply;

        grpc::ClientContext context;
        grpc::Status status = stub_->TrustedValidation(&context, request, &reply);


        if (! status.ok()) {
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            return false;
        }

        std::cout << txid.decompress(true) << ": " << ((reply.valid()) ? "valid" : "invalid") << "\n";

        return true;
    }

    bool OutputOracle(const std::string& txid_str, const uint32_t vout)
    {
        {
            static const std::regex txid_regex("^[0-9a-fA-F]{64}$");
            const bool rmatch = std::regex_match(txid_str, txid_regex);
            if (! rmatch) {
                std::cerr << "txid did not match regex\n";
                return false;
            }
        }

        graphsearch::OutputOracleRequest request;

        gs::txid txid(txid_str);
        std::reverse(txid.v.begin(), txid.v.end());

        request.set_txid(txid.decompress());
        request.set_vout(vout);

        graphsearch::OutputOracleReply reply;

        grpc::ClientContext context;
        grpc::Status status = stub_->OutputOracle(&context, request, &reply);


        if (! status.ok()) {
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            return false;
        }

        const uint32_t tokentype = reply.tokentype();

        if (tokentype == 0x01) {
            std::cout
                << "outpoint: " << txid.decompress(true) << ":" << reply.vout() << "\n"
                << "msg: " << gs::util::hex(reply.msg().begin(), reply.msg().end()) << "\n"
                << "sig: " << gs::util::hex(reply.sig().begin(), reply.sig().end()) << "\n"
                << "tx: " << gs::util::hex(reply.tx().begin(),  reply.tx().end())  << "\n"
                << "tokenid: " << gs::tokenid(std::vector<std::uint8_t>(reply.tokenid().begin(), reply.tokenid().end())).decompress(true) << "\n"
                << "tokentype: " << reply.tokentype() << "\n"
                << "value: " << reply.value() << "\n"
                << "baton: " << (reply.is_baton() ? "true" : "false") << "\n";

        }

        if (tokentype == 0x81) {
            std::cout
                << "outpoint: " << txid.decompress(true) << ":" << reply.vout() << "\n"
                << "msg: " << gs::util::hex(reply.msg().begin(), reply.msg().end()) << "\n"
                << "sig: " << gs::util::hex(reply.sig().begin(), reply.sig().end()) << "\n"
                << "tx: " << gs::util::hex(reply.tx().begin(),  reply.tx().end())  << "\n"
                << "tokenid: " << gs::tokenid(std::vector<std::uint8_t>(reply.tokenid().begin(), reply.tokenid().end())).decompress(true) << "\n"
                << "tokentype: " << reply.tokentype() << "\n"
                << "value: " << reply.value() << "\n"
                << "baton: " << (reply.is_baton() ? "true" : "false") << "\n";

        }

        if (tokentype == 0x41) {
            std::cout
                << "outpoint: " << txid.decompress(true) << ":" << reply.vout() << "\n"
                << "msg: " << gs::util::hex(reply.msg().begin(), reply.msg().end()) << "\n"
                << "sig: " << gs::util::hex(reply.sig().begin(), reply.sig().end()) << "\n"
                << "tx: " << gs::util::hex(reply.tx().begin(),  reply.tx().end())  << "\n"
                << "tokenid: " << gs::tokenid(std::vector<std::uint8_t>(reply.tokenid().begin(), reply.tokenid().end())).decompress(true) << "\n"
                << "tokentype: " << reply.tokentype() << "\n"
                << "groupid: " << gs::tokenid(std::vector<std::uint8_t>(reply.groupid().begin(), reply.groupid().end())).decompress(true) << "\n";

        }

        return true;
    }

    bool Status() {
        graphsearch::StatusRequest request;
        graphsearch::StatusReply reply;

        grpc::ClientContext context;
        grpc::Status status = stub_->Status(&context, request, &reply);
        if (! status.ok()) {
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            return false;
        }

        std::cout
            << "current_block_height:       " << reply.block_height()               << "\n"
            << "best_block_hash:            " << reply.best_block_hash()            << "\n"
            << "last_incoming_zmq_tx_unix:  " << reply.last_incoming_zmq_tx_unix()  << "\n"
            << "last_outgoing_zmq_tx_unix:  " << reply.last_outgoing_zmq_tx_unix()  << "\n"
            << "last_incoming_zmq_tx:       " << reply.last_incoming_zmq_tx()       << "\n"
            << "last_outgoing_zmq_tx:       " << reply.last_outgoing_zmq_tx()       << "\n"
            << "last_incoming_zmq_blk_unix: " << reply.last_incoming_zmq_blk_unix() << "\n"
            << "last_outgoing_zmq_blk_unix: " << reply.last_outgoing_zmq_blk_unix() << "\n"
            << "last_incoming_zmq_blk_size: " << reply.last_incoming_zmq_blk_size() << "\n"
            << "last_outgoing_zmq_blk_size: " << reply.last_outgoing_zmq_blk_size() << "\n";

        return true;
    }

private:
    std::unique_ptr<graphsearch::GraphSearchService::Stub> stub_;
};


class UtxoServiceClient
{
public:
    UtxoServiceClient(std::shared_ptr<grpc::Channel> channel)
    : stub_(graphsearch::UtxoService::NewStub(channel))
    {}

    bool UtxoSearchByOutpoints(
        const std::vector<std::pair<std::string, std::uint32_t>> outpoints
    ) {
        graphsearch::UtxoSearchByOutpointsRequest request;

        for (auto o : outpoints) {
            auto * outpoint = request.add_outpoints();
            outpoint->set_txid(o.first);
            outpoint->set_vout(o.second);
        }

        graphsearch::UtxoSearchReply reply;

        grpc::ClientContext context;
        grpc::Status status = stub_->UtxoSearchByOutpoints(&context, request, &reply);
        if (! status.ok()) {
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            return false;
        }

        for (auto n : reply.outputs()) {
            const std::string   prev_tx_id_str   = n.prev_tx_id();
            const std::uint32_t prev_out_idx     = n.prev_out_idx();
            const std::uint64_t value            = n.value();
            const std::string   scriptpubkey_str = n.scriptpubkey();

            gs::txid prev_tx_id(prev_tx_id_str);

            std::string scriptpubkey_b64(scriptpubkey_str.size()*1.5, '\0');
            std::size_t scriptpubkey_b64_len = 0;
            base64_encode(
                scriptpubkey_str.data(),
                scriptpubkey_str.size(),
                const_cast<char*>(scriptpubkey_b64.data()),
                &scriptpubkey_b64_len,
                0
            );
            scriptpubkey_b64.resize(scriptpubkey_b64_len);

            std::cout
                << prev_tx_id.decompress(true) << ":" << prev_out_idx << "\n"
                << "\tvalue:        " << value                        << "\n"
                << "\tscriptpubkey: " << scriptpubkey_b64             << "\n";
        }

        return true;
    }

    bool UtxoSearchByScriptPubKey(const gs::scriptpubkey scriptpubkey)
    {
        graphsearch::UtxoSearchByScriptPubKeyRequest request;
        request.set_scriptpubkey(std::string(scriptpubkey.v.begin(), scriptpubkey.v.end()));

        graphsearch::UtxoSearchReply reply;

        grpc::ClientContext context;
        grpc::Status status = stub_->UtxoSearchByScriptPubKey(&context, request, &reply);
        if (! status.ok()) {
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            return false;
        }

        for (auto n : reply.outputs()) {
            const std::string   prev_tx_id_str   = n.prev_tx_id();
            const std::uint32_t prev_out_idx     = n.prev_out_idx();
            const std::uint64_t value            = n.value();
            const std::string   scriptpubkey_str = n.scriptpubkey();

            gs::txid prev_tx_id(prev_tx_id_str);

            std::string scriptpubkey_b64(scriptpubkey_str.size()*1.5, '\0');
            std::size_t scriptpubkey_b64_len = 0;
            base64_encode(
                scriptpubkey_str.data(),
                scriptpubkey_str.size(),
                const_cast<char*>(scriptpubkey_b64.data()),
                &scriptpubkey_b64_len,
                0
            );
            scriptpubkey_b64.resize(scriptpubkey_b64_len);

            std::cout
                << prev_tx_id.decompress(true) << ":" << prev_out_idx << "\n"
                << "\tvalue: " << value                               << "\n";
        }

        return true;
    }

    bool BalanceByScriptPubKey(const gs::scriptpubkey scriptpubkey)
    {
        graphsearch::BalanceByScriptPubKeyRequest request;
        request.set_scriptpubkey(std::string(scriptpubkey.v.begin(), scriptpubkey.v.end()));

        graphsearch::BalanceByScriptPubKeyReply reply;

        grpc::ClientContext context;
        grpc::Status status = stub_->BalanceByScriptPubKey(&context, request, &reply);
        if (! status.ok()) {
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            return false;
        }

        const std::uint64_t balance = reply.balance();
        std::cout << balance << "\n";
        return true;
    }


private:
    std::unique_ptr<graphsearch::UtxoService::Stub> stub_;
};

void validatefile(std::string src)
{
    std::ifstream infile(src);

    std::vector<gs::transaction> txs;

    std::string line;
    while (std::getline(infile, line)) {
        std::string decoded(line.size(), '\0');
        std::size_t len = 0;
        base64_decode(
            line.data(),
            line.size(),
            const_cast<char*>(decoded.data()),
            &len,
            0
        );
        decoded.resize(len);

        gs::transaction tx;
        if (! tx.hydrate(decoded.begin(), decoded.end())) {
            std::cerr << "ERROR: could not hydrate from txdata\n";
            continue;
        }

        txs.push_back(tx);
    }

    txs = gs::util::topological_sort(txs);

    gs::slp_validator validator;
    for (auto & tx : txs) {
        std::cout << tx.txid.decompress(true) << "\n";
        validator.add_tx(tx);
    }

    gs::txid txid = txs.back().txid;
    const bool valid = validator.validate(txid);
    std::cout << txid.decompress(true) << ": " << ((valid) ? "valid" : "invalid") << "\n";
}

int main(int argc, char* argv[])
{
    std::string grpc_host = "0.0.0.0";
    std::string grpc_port = "50051";
    std::string query_type = "graphsearch";
    bool use_tls = false;
    std::vector<std::string> exclude_txids;

    const std::string usage_str = "usage: gs++-cli [--version] [--help] [--host host_address] [--port port] [--use_tls]\n"
                                  "[--graphsearch TXID] [--utxo TXID:VOUT] [--utxo_scriptpubkey PK]\n"
                                  "[--balance_scriptpubkey PK] [--validate TXID] [--tvalidate TXID]\n"
                                  "[--status]\n";

    while (true) {
        static struct option long_options[] = {
            { "help",    no_argument,       nullptr, 'h' },
            { "version", no_argument,       nullptr, 'v' },
            { "host",    required_argument, nullptr, 'b' },
            { "port",    required_argument, nullptr, 'p' },
            { "use_tls", no_argument,       nullptr, 's' },

            { "graphsearch",          no_argument,       nullptr, 1000 },
            { "utxo",                 no_argument,       nullptr, 1001 },
            { "utxo_scriptpubkey",    no_argument,       nullptr, 1002 },
            { "balance_scriptpubkey", no_argument,       nullptr, 1003 },
            { "validate",             no_argument,       nullptr, 1004 },
            { "tvalidate",            no_argument,       nullptr, 1005 },
            { "validatefile",         required_argument, nullptr, 1006 },
            { "status",               no_argument,       nullptr, 1007 },
            { "dot",                  no_argument,       nullptr, 1008 },
            { "outputoracle",         no_argument,       nullptr, 1009 },
            { "exclude",              required_argument, nullptr, 2000 },
            { 0, 0, nullptr, 0 },
        };

        int option_index = 0;
        int c = getopt_long(argc, argv, "hvb:p:s", long_options, &option_index);

        if (c == -1) {
            break;
        }


        std::stringstream ss(optarg != nullptr ? optarg : "");
        std::string tmp; // used for anonymous parse
        switch (c) {
            case 0:
                if (long_options[option_index].flag != 0) {
                    break;
                }

                break;
            case 'h':
                std::cout << usage_str;
                return EXIT_SUCCESS;
            case 'v':
                std::cout <<
                    "gs++-cli v" << PROJECT_VERSION << std::endl;
                return EXIT_SUCCESS;
            case 'b': ss >> grpc_host; break;
            case 'p': ss >> grpc_port; break;
            case 's': use_tls = true;  break;

            case 1000: query_type = "graphsearch";          break;
            case 1001: query_type = "utxo";                 break;
            case 1002: query_type = "utxo_scriptpubkey";    break;
            case 1003: query_type = "balance_scriptpubkey"; break;
            case 1004: query_type = "validate";             break;
            case 1005: query_type = "tvalidate";            break;
            case 1006: query_type = "validatefile";         break;
            case 1007: query_type = "status";               break;
            case 1008: query_type = "dot";                  break;
            case 1009: query_type = "outputoracle";         break;
            case 2000:
                ss >> tmp;
                exclude_txids.push_back(tmp);
                break;

            case '?':
                return EXIT_FAILURE;
            default:
                return EXIT_FAILURE;
        }
    }
    if (argc < 2) {
        std::cout << usage_str;
        return EXIT_FAILURE;
    }

    grpc::ChannelArguments ch_args;
    ch_args.SetMaxReceiveMessageSize(-1);

    const auto channel_creds = use_tls
        ? grpc::SslCredentials(grpc::SslCredentialsOptions())
        : grpc::InsecureChannelCredentials();

    const auto channel = grpc::CreateCustomChannel(grpc_host+":"+grpc_port, channel_creds, ch_args);

    GraphSearchServiceClient graphsearch_client(channel);
    UtxoServiceClient utxo_client(channel);

    if (query_type == "graphsearch") {
        graphsearch_client.GraphSearch(argv[argc-1], exclude_txids);
    } else if (query_type == "dot") {
        graphsearch_client.Dot(argv[argc-1], exclude_txids);
    } else if (query_type == "status") {
        graphsearch_client.Status();
    } else if (query_type == "validate") {
        graphsearch_client.GraphSearchValidate(argv[argc-1]);
    } else if (query_type == "tvalidate") {
        graphsearch_client.GraphSearchTrustedValidate(argv[argc-1]);
    } else if (query_type == "validatefile") {
        validatefile(argv[argc-1]);
    } else if (query_type == "outputoracle") {
        std::vector<std::string> elems;
        {
            std::string outpoint_str(argv[argc-1]);
            std::stringstream ss(outpoint_str);
            std::string v;
            while(std::getline(ss, v, ':')) {
                elems.push_back(v);
            }

            if (elems.size() != 2) {
                std::cerr << "bad format: expected TXID:VOUT\n";
                return EXIT_FAILURE;
            }
        }

        const std::string txid = elems[0];
        const uint32_t    vout = static_cast<uint32_t>(std::stoul(elems[1]));

        graphsearch_client.OutputOracle(txid, vout);
    } else if (query_type == "utxo") {
        std::vector<std::pair<std::string, std::uint32_t>> outpoints;
        for (int optidx=optind; optidx < argc; ++optidx) {
            std::vector<std::string> seglist;
            {
                std::stringstream ss(argv[optidx]);
                std::string segment;

                while (std::getline(ss, segment, ':')) {
                    seglist.push_back(segment);
                }
            }

            if (seglist.size() != 2) {
                std::cerr << "bad format: expected TXID:VOUT\n";
                return EXIT_FAILURE;
            }

            gs::txid txid(seglist[0]);
            std::reverse(txid.v.begin(), txid.v.end());
            std::uint32_t vout = 0;
            {
                std::stringstream ss(seglist[1]);
                ss >> vout;
            }

            outpoints.push_back({ txid.decompress(), vout });
        }

        utxo_client.UtxoSearchByOutpoints(outpoints);
    } else if (query_type == "utxo_scriptpubkey") {
        const std::string scriptpubkey_b64 = argv[optind];

        std::size_t scriptpubkey_len = 0;
        std::string decoded(scriptpubkey_b64.size(), '\0');
        base64_decode(
            scriptpubkey_b64.data(),
            scriptpubkey_b64.size(),
            const_cast<char*>(decoded.data()),
            &scriptpubkey_len,
            0
        );
        decoded.resize(scriptpubkey_len);

        utxo_client.UtxoSearchByScriptPubKey(gs::scriptpubkey(decoded));
    } else if (query_type == "balance_scriptpubkey") {
        const std::string scriptpubkey_b64 = argv[optind];

        std::size_t scriptpubkey_len = 0;
        std::string decoded(scriptpubkey_b64.size(), '\0');
        base64_decode(
            scriptpubkey_b64.data(),
            scriptpubkey_b64.size(),
            const_cast<char*>(decoded.data()),
            &scriptpubkey_len,
            0
        );
        decoded.resize(scriptpubkey_len);

        utxo_client.BalanceByScriptPubKey(gs::scriptpubkey(decoded));
    }

    return EXIT_SUCCESS;
}

