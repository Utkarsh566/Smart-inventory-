#pragma once
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Prefix index: key (lowercase token) -> product ids.
class Trie {
    struct Node {
        std::unordered_map<char, std::unique_ptr<Node>> next;
        std::vector<std::string> ids;
    };
    Node root_;

    static void collect(const Node* n, std::unordered_set<std::string>& out) {
        for (const auto& id : n->ids) out.insert(id);
        for (const auto& kv : n->next) collect(kv.second.get(), out);
    }

public:
    void insert(const std::string& key, const std::string& id) {
        Node* n = &root_;
        for (char c : key) {
            auto& child = n->next[c];
            if (!child) child = std::make_unique<Node>();
            n = child.get();
        }
        if (std::find(n->ids.begin(), n->ids.end(), id) == n->ids.end()) n->ids.push_back(id);
    }

    void erase(const std::string& key, const std::string& id) {
        Node* n = &root_;
        for (char c : key) {
            auto it = n->next.find(c);
            if (it == n->next.end()) return;
            n = it->second.get();
        }
        n->ids.erase(std::remove(n->ids.begin(), n->ids.end(), id), n->ids.end());
    }

    std::unordered_set<std::string> withPrefix(const std::string& prefix) const {
        const Node* n = &root_;
        for (char c : prefix) {
            auto it = n->next.find(c);
            if (it == n->next.end()) return {};
            n = it->second.get();
        }
        std::unordered_set<std::string> out;
        collect(n, out);
        return out;
    }
};
