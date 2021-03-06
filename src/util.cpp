#include <vector>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <gs++/transaction.hpp>
#include <gs++/bhash.hpp>
#include <gs++/output.hpp>

namespace gs {

namespace util {

void topological_sort_internal(
    const gs::transaction& tx,
    const absl::flat_hash_map<gs::txid, gs::transaction> & transactions,
    std::vector<gs::transaction> & stack,
    absl::flat_hash_set<gs::txid> & visited
) {
    visited.insert(tx.txid);

    for (const gs::outpoint & outpoint : tx.inputs) {
        if (visited.count(outpoint.txid)      != 0
        || transactions.count(outpoint.txid)  != 1
        ) {
            continue;
        }

        topological_sort_internal(
            transactions.at(outpoint.txid),
            transactions,
            stack,
            visited
        );
    }

    stack.push_back(tx);
}

std::vector<gs::transaction> topological_sort(
    const std::vector<gs::transaction>& tx_list
) {
    absl::flat_hash_map<gs::txid, gs::transaction> transactions;
    transactions.reserve(tx_list.size());
    for (const gs::transaction & tx : tx_list) {
        transactions.insert({ tx.txid, tx });
    }

    std::vector<gs::transaction> stack;
    stack.reserve(tx_list.size());

    absl::flat_hash_set<gs::txid> visited;
    visited.reserve(tx_list.size());

    for (const gs::transaction & tx : tx_list) {
        if (visited.count(tx.txid) == 0) {
            topological_sort_internal(tx, transactions, stack, visited);
        }
    }

    return stack;
}

std::vector<std::uint8_t> num_to_var_int(const std::uint64_t n)
{
    // TODO test this its probably broken
    auto B = [&n](const std::size_t m) -> std::uint8_t {
        return (n >> (8*m)) & 0xFF;
    };

    if (n <= 0xFC)       return { static_cast<std::uint8_t>(n) };
    if (n <= 0xFFFF)     return { 0xFD, B(0), B(1) };
    if (n <= 0xFFFFFFFF) return { 0xFE, B(0), B(1), B(2), B(3) };
    return { 0xFF, B(0), B(1), B(2), B(3), B(4), B(5), B(6), B(7) };
}

}

}
