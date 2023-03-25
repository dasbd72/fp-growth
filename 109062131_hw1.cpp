#include <omp.h>
#include <pthread.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifdef DEBUG
#define DEBUG_MSG(str) std::cout << str << "\n";
#else
#define DEBUG_MSG(str)
#endif  // DEBUG

#ifdef TIMING
#include <ctime>
#define TIMING_START(arg)          \
    struct timespec __start_##arg; \
    clock_gettime(CLOCK_MONOTONIC, &__start_##arg);
#define TIMING_END(arg)                                                                       \
    {                                                                                         \
        struct timespec __temp_##arg, __end_##arg;                                            \
        double __duration_##arg;                                                              \
        clock_gettime(CLOCK_MONOTONIC, &__end_##arg);                                         \
        if ((__end_##arg.tv_nsec - __start_##arg.tv_nsec) < 0) {                              \
            __temp_##arg.tv_sec = __end_##arg.tv_sec - __start_##arg.tv_sec - 1;              \
            __temp_##arg.tv_nsec = 1000000000 + __end_##arg.tv_nsec - __start_##arg.tv_nsec;  \
        } else {                                                                              \
            __temp_##arg.tv_sec = __end_##arg.tv_sec - __start_##arg.tv_sec;                  \
            __temp_##arg.tv_nsec = __end_##arg.tv_nsec - __start_##arg.tv_nsec;               \
        }                                                                                     \
        __duration_##arg = __temp_##arg.tv_sec + (double)__temp_##arg.tv_nsec / 1000000000.0; \
        std::cout << #arg << " took " << __duration_##arg << "s.\n";                          \
        std::cout.flush();                                                                    \
    }
#define TIMING_INIT(arg) \
    double __duration_##arg = 0;
#define TIMING_ACCUM(arg)                                                                      \
    {                                                                                          \
        struct timespec __temp_##arg, __end_##arg;                                             \
        clock_gettime(CLOCK_MONOTONIC, &__end_##arg);                                          \
        if ((__end_##arg.tv_nsec - __start_##arg.tv_nsec) < 0) {                               \
            __temp_##arg.tv_sec = __end_##arg.tv_sec - __start_##arg.tv_sec - 1;               \
            __temp_##arg.tv_nsec = 1000000000 + __end_##arg.tv_nsec - __start_##arg.tv_nsec;   \
        } else {                                                                               \
            __temp_##arg.tv_sec = __end_##arg.tv_sec - __start_##arg.tv_sec;                   \
            __temp_##arg.tv_nsec = __end_##arg.tv_nsec - __start_##arg.tv_nsec;                \
        }                                                                                      \
        __duration_##arg += __temp_##arg.tv_sec + (double)__temp_##arg.tv_nsec / 1000000000.0; \
    }
#define TIMING_FIN(arg)                                          \
    std::cout << #arg << " took " << __duration_##arg << "s.\n"; \
    std::cout.flush();
TIMING_INIT(fpgrowth_combination);
#else
#define TIMING_START(arg)
#define TIMING_END(arg)
#endif  // TIMING

#define MAXFREQ 20000000
#define MAXTRXNS 100000
#define MAXTRXN 200
#define MAXITEM 1000
#define NULLITEM -1
#define REFINC(_ref) [&](int x, int y) { \
    if (_ref[x] != _ref[y])              \
        return _ref[x] < _ref[y];        \
    else                                 \
        return x > y;                    \
}
#define REFDEC(_ref) [&](int x, int y) { \
    if (_ref[x] != _ref[y])              \
        return _ref[x] > _ref[y];        \
    else                                 \
        return x < y;                    \
}

using Item = int;
using Transaction = std::vector<Item>;
using Pattern = std::pair<Transaction, int>;

struct FPNode {
    // Metadata
    Item item;
    int cnt;
    // link list data (same item)
    FPNode* next;
    // tree data
    FPNode* parent;
    std::map<Item, FPNode*> child;

    FPNode(Item item) : item(item), cnt(0), next(NULL), parent(NULL) {}
};
struct FPTree {
    FPNode* root;
    std::unordered_map<Item, FPNode*> hdr_table;
    std::vector<Item> items_by_freq;          // items above minimum support, sorts decreasing by frequency
    std::unordered_map<Item, int> item_freq;  // item frequency count
    bool singlePath;

    FPTree() {
        root = new FPNode(-1);
        singlePath = true;
    }
    ~FPTree() {
        deleteTree(root);
    };

    void insertPath(const Transaction& trxn, std::unordered_map<Item, FPNode*>& tail_table, const int& inc);
    void buildFromTrxns(const std::vector<Transaction>& trxns, const int& min_sup);
    void growth(FPNode* preroot, FPNode* prehead, const int& min_sup);
    void fpgrowthCombination(int idx, std::vector<Item>& lst, const std::string& base, std::ofstream& output_file, const size_t& trxns_size);
    void fpgrowth(const std::string& output_filename, const int& min_sup, const size_t& trxns_size);
    void fpgrowth(Transaction& base, const int& min_sup, std::ofstream& output_file, const size_t& trxns_size);

    bool empty();
    bool hasSinglePath();
    // Debug use, output traverse of fptree
    void traverse(FPNode* node);
    // Delete subtree from node
    void deleteTree(FPNode* node);
};

// Read transactions from file and count item frequencies
void read_transactions(const std::string& input_filename, std::vector<Transaction>& trxns, FPTree& fptree);

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(0);

    assert(argc == 4);
    double fmin_sup = atof(argv[1]);
    std::string input_filename = argv[2];
    std::string output_filename = argv[3];
    int n_cpus = omp_get_max_threads();

    DEBUG_MSG("Cpus: " << n_cpus);

    int min_sup;
    FPTree fptree;
    std::vector<Transaction> trxns;  // transaction list

    // input
    // build trxns and item_freq
    // first scan, count freq and load transactions
    TIMING_START(total);
    TIMING_START(input);
    read_transactions(input_filename, trxns, fptree);
    min_sup = ceil(fmin_sup * trxns.size());  // transform min support percent to min support count
    TIMING_END(input);

    // construct fptree
    // set min_sup, build fptree
    // second scan, build fptree and update table
    TIMING_START(build_fptree);
    fptree.buildFromTrxns(trxns, min_sup);
    TIMING_END(build_fptree);

    // output once a pattern is found
    TIMING_START(fpgrowth_and_output);
    fptree.fpgrowth(output_filename, min_sup, trxns.size());
    TIMING_END(fpgrowth_and_output);
    TIMING_FIN(fpgrowth_combination);
    TIMING_END(total);

    return 0;
}

void FPTree::insertPath(const Transaction& trxn, std::unordered_map<Item, FPNode*>& tail_table, const int& inc) {
    FPNode* curr = root;
    for (int j = (int)trxn.size() - 1; j >= 0; j--) {
        Item item = trxn[j];
        auto it = curr->child.find(item);
        // if exist move curr, else create and move
        if (it != curr->child.end()) {
            curr = it->second;
        } else {
            if (curr->child.size() >= 1)
                singlePath = false;
            FPNode* node = new FPNode(item);
            node->parent = curr;
            curr = curr->child[item] = node;
            if (tail_table.find(item) == tail_table.end()) {
                hdr_table[item] = tail_table[item] = node;
            } else {
                tail_table[item] = tail_table[item]->next = node;
            }
        }
        curr->cnt += inc;
    }
}

void FPTree::buildFromTrxns(const std::vector<Transaction>& trxns, const int& min_sup) {
    for (auto& pair : item_freq) {
        if (pair.second >= min_sup) {
            items_by_freq.emplace_back(pair.first);
        }
    }
    std::sort(items_by_freq.begin(), items_by_freq.end(), REFDEC(item_freq));

    std::unordered_map<Item, FPNode*> tail_table;
    for (int i = 0; i < (int)trxns.size(); i++) {
        Transaction trxn;
        // prune infrequent items
        for (int j = 0; j < (int)trxns[i].size(); j++) {
            Item item = trxns[i][j];
            if (item_freq[item] >= min_sup)
                trxn.emplace_back(item);
        }
        // sort transaction by freq
        std::sort(trxn.begin(), trxn.end(), REFINC(item_freq));

        // add path to fptree
        insertPath(trxn, tail_table, 1);
    }
}

void FPTree::growth(FPNode* preroot, FPNode* prehead, const int& min_sup) {
    // count frequency of current tree
    for (FPNode* leaf = prehead; leaf != NULL; leaf = leaf->next) {
        for (FPNode* curr = leaf->parent; curr != preroot; curr = curr->parent) {
            item_freq[curr->item] += leaf->cnt;
        }
    }
    for (auto& pair : item_freq) {
        if (pair.second >= min_sup) {
            items_by_freq.emplace_back(pair.first);
        }
    }
    std::sort(items_by_freq.begin(), items_by_freq.end(), REFDEC(item_freq));

    // construct conditional tree
    std::unordered_map<Item, FPNode*> tail_table;
    for (FPNode* leaf = prehead; leaf != NULL; leaf = leaf->next) {
        // get path
        Transaction trxn;
        for (FPNode* curr = leaf->parent; curr != preroot; curr = curr->parent) {
            if (item_freq[curr->item] >= min_sup)
                trxn.emplace_back(curr->item);
        }

        // add path to conditional tree
        insertPath(trxn, tail_table, leaf->cnt);
    }
}

void FPTree::fpgrowthCombination(int idx, std::vector<Item>& lst, const std::string& base_str, std::ofstream& output_file, const size_t& trxns_size) {
    if (idx == (int)items_by_freq.size())
        return;

    // Choose
    Item currItem = items_by_freq[idx];
    lst.emplace_back(currItem);

    // output current combination
    std::ostringstream oss;
    oss << *lst.begin();
    std::for_each(lst.begin() + 1, lst.end(), [&](const auto& item) {
        oss << ',' << item;
    });
    oss << base_str;
    oss << std::fixed << std::setprecision(4) << ':' << (double)item_freq[currItem] / trxns_size << '\n';
    auto str = oss.str();
    output_file.write(str.data(), str.size());

    fpgrowthCombination(idx + 1, lst, base_str, output_file, trxns_size);
    lst.pop_back();

    // Not choose
    fpgrowthCombination(idx + 1, lst, base_str, output_file, trxns_size);
}

void FPTree::fpgrowth(const std::string& output_filename, const int& min_sup, const size_t& trxns_size) {
    std::ofstream output_file(output_filename);
    if (output_file.is_open()) {
        Transaction base;
        fpgrowth(base, min_sup, output_file, trxns_size);
        output_file.close();
    }
}

void FPTree::fpgrowth(Transaction& base, const int& min_sup, std::ofstream& output_file, const size_t& trxns_size) {
    if (hasSinglePath()) {
        std::ostringstream oss;
        std::for_each(base.begin(), base.end(), [&](const auto& item) {
            oss << ',' << item;
        });
        auto base_str = oss.str();
        std::vector<Item> lst;
        TIMING_START(fpgrowth_combination);
        fpgrowthCombination(0, lst, base_str, output_file, trxns_size);
        TIMING_ACCUM(fpgrowth_combination);
    } else {
        for (int i = (int)items_by_freq.size() - 1; i >= 0; i--) {
            Item baseItem = items_by_freq[i];
            base.emplace_back(baseItem);

            // output base
            std::ostringstream oss;
            oss << *base.begin();
            std::for_each(base.begin() + 1, base.end(), [&](const auto& item) {
                oss << ',' << item;
            });
            oss << std::fixed << std::setprecision(4) << ':' << (double)item_freq[baseItem] / trxns_size << '\n';
            auto str = oss.str();
            output_file.write(str.data(), str.size());

            // build conditional fptree
            FPTree cond_fptree;
            cond_fptree.growth(root, hdr_table[baseItem], min_sup);

            if (!cond_fptree.empty()) {
                cond_fptree.fpgrowth(base, min_sup, output_file, trxns_size);
            }
            base.pop_back();
        }
    }
}

bool FPTree::empty() {
    return root->child.size() == 0;
}

bool FPTree::hasSinglePath() {
    return singlePath;
}

void FPTree::traverse(FPNode* node) {
    if (node == NULL)
        return;
    for (auto child : node->child) {
        std::cout << child.second->item << " " << child.second->cnt << "\n";
        traverse(child.second);
    }
}

void FPTree::deleteTree(FPNode* node) {
    if (node == NULL)
        return;
    for (auto child : node->child)
        deleteTree(child.second);
    delete node;
}

void read_transactions(const std::string& input_filename, std::vector<Transaction>& trxns, FPTree& fptree) {
    std::ifstream input_file(input_filename);
    std::string str;
    trxns.reserve(MAXTRXNS);
    if (input_file.is_open()) {
        while (getline(input_file, str)) {
            Transaction trxn;
            trxn.reserve(MAXTRXN);
            size_t start = 0;
            size_t end = str.find(',');

            while (true) {
                Item item = std::stoi(str.substr(start, end - start));
                trxn.emplace_back(item);
                fptree.item_freq[item]++;
                if (end == std::string::npos)
                    break;
                start = end + 1;
                end = str.find(',', start);
            }
            trxns.emplace_back(trxn);
        }
        input_file.close();
    }
}