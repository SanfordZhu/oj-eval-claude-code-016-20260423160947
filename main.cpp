#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <set>

const int MAX_KEYS = 50;  // Smaller size for better performance
const int MIN_KEYS = 25;
const std::string DATA_FILE = "data.bpt";

// Node structure for B+ tree
struct Node {
    bool is_leaf;
    int key_count;
    std::string keys[MAX_KEYS];
    int values[MAX_KEYS];  // Only used for leaf nodes
    int children[MAX_KEYS + 1]; // Only used for internal nodes (file offsets)
    int next_leaf; // Only used for leaf nodes (file offset of next leaf)

    Node() : is_leaf(true), key_count(0), next_leaf(-1) {
        for (int i = 0; i <= MAX_KEYS; i++) {
            children[i] = -1;
        }
    }
};

// File-based B+ tree implementation
class BPTree {
private:
    std::fstream file;
    int root_offset;

    // Write node to file at specified offset
    void write_node(int offset, const Node& node) {
        file.seekp(offset);
        file.write(reinterpret_cast<const char*>(&node.is_leaf), sizeof(bool));
        file.write(reinterpret_cast<const char*>(&node.key_count), sizeof(int));

        // Write keys
        for (int i = 0; i < node.key_count; i++) {
            int key_len = node.keys[i].length();
            file.write(reinterpret_cast<const char*>(&key_len), sizeof(int));
            file.write(node.keys[i].c_str(), key_len);
        }

        if (node.is_leaf) {
            // Write values for leaf nodes
            for (int i = 0; i < node.key_count; i++) {
                file.write(reinterpret_cast<const char*>(&node.values[i]), sizeof(int));
            }
            file.write(reinterpret_cast<const char*>(&node.next_leaf), sizeof(int));
        } else {
            // Write children offsets for internal nodes
            for (int i = 0; i <= node.key_count; i++) {
                file.write(reinterpret_cast<const char*>(&node.children[i]), sizeof(int));
            }
        }
    }

    // Read node from file at specified offset
    Node read_node(int offset) {
        Node node;
        file.seekg(offset);
        file.read(reinterpret_cast<char*>(&node.is_leaf), sizeof(bool));
        file.read(reinterpret_cast<char*>(&node.key_count), sizeof(int));

        // Read keys
        for (int i = 0; i < node.key_count; i++) {
            int key_len;
            file.read(reinterpret_cast<char*>(&key_len), sizeof(int));
            char* buffer = new char[key_len + 1];
            file.read(buffer, key_len);
            buffer[key_len] = '\0';
            node.keys[i] = std::string(buffer);
            delete[] buffer;
        }

        if (node.is_leaf) {
            // Read values for leaf nodes
            for (int i = 0; i < node.key_count; i++) {
                file.read(reinterpret_cast<char*>(&node.values[i]), sizeof(int));
            }
            file.read(reinterpret_cast<char*>(&node.next_leaf), sizeof(int));
        } else {
            // Read children offsets for internal nodes
            for (int i = 0; i <= node.key_count; i++) {
                file.read(reinterpret_cast<char*>(&node.children[i]), sizeof(int));
            }
        }

        return node;
    }

    // Find the appropriate leaf node for the given key
    int find_leaf(const std::string& key) {
        if (root_offset == -1) return -1;

        int current_offset = root_offset;
        Node current = read_node(current_offset);

        while (!current.is_leaf) {
            int i = 0;
            while (i < current.key_count && key >= current.keys[i]) {
                i++;
            }
            current_offset = current.children[i];
            current = read_node(current_offset);
        }

        return current_offset;
    }

    // Split a full node
    int split_node(int node_offset, Node& node) {
        int new_offset = file.tellp();
        Node new_node;

        if (node.is_leaf) {
            // Split leaf node
            int split_point = (node.key_count + 1) / 2;

            // Move half of the keys and values to new node
            for (int i = split_point; i < node.key_count; i++) {
                new_node.keys[i - split_point] = node.keys[i];
                new_node.values[i - split_point] = node.values[i];
            }
            new_node.key_count = node.key_count - split_point;
            new_node.is_leaf = true;
            new_node.next_leaf = node.next_leaf;

            // Update original node
            node.key_count = split_point;
            node.next_leaf = new_offset;

            // Write both nodes
            write_node(node_offset, node);
            write_node(new_offset, new_node);

            return new_offset;
        } else {
            // Split internal node
            int split_point = node.key_count / 2;

            // Move half of the keys and children to new node
            for (int i = split_point + 1; i < node.key_count; i++) {
                new_node.keys[i - split_point - 1] = node.keys[i];
            }
            for (int i = split_point + 1; i <= node.key_count; i++) {
                new_node.children[i - split_point - 1] = node.children[i];
            }
            new_node.key_count = node.key_count - split_point - 1;
            new_node.is_leaf = false;

            // Update original node
            node.key_count = split_point;

            // Write both nodes
            write_node(node_offset, node);
            write_node(new_offset, new_node);

            return new_offset;
        }
    }

    // Insert into non-full node
    void insert_non_full(int node_offset, Node& node, const std::string& key, int value) {
        if (node.is_leaf) {
            // Insert into leaf node
            // Check if key-value pair already exists
            for (int j = 0; j < node.key_count; j++) {
                if (node.keys[j] == key && node.values[j] == value) {
                    return; // Duplicate key-value pair, don't insert
                }
            }

            // Insert at correct position
            int i = node.key_count - 1;
            while (i >= 0 && key < node.keys[i]) {
                node.keys[i + 1] = node.keys[i];
                node.values[i + 1] = node.values[i];
                i--;
            }
            i++;

            node.keys[i] = key;
            node.values[i] = value;
            node.key_count++;

            write_node(node_offset, node);
        } else {
            // Find appropriate child
            int i = 0;
            while (i < node.key_count && key >= node.keys[i]) {
                i++;
            }

            int child_offset = node.children[i];
            Node child = read_node(child_offset);

            if (child.key_count == MAX_KEYS) {
                // Child is full, split it
                int new_child_offset = split_node(child_offset, child);
                Node new_child = read_node(new_child_offset);

                // Update current node
                // Shift keys and children to make room
                for (int j = node.key_count; j > i; j--) {
                    node.keys[j] = node.keys[j - 1];
                }
                for (int j = node.key_count + 1; j > i + 1; j--) {
                    node.children[j] = node.children[j - 1];
                }

                node.keys[i] = new_child.keys[0];
                node.children[i + 1] = new_child_offset;
                node.key_count++;

                write_node(node_offset, node);

                // Determine which child to insert into
                if (key >= node.keys[i]) {
                    child_offset = new_child_offset;
                    child = new_child;
                }
            }

            insert_non_full(child_offset, child, key, value);
        }
    }

public:
    BPTree() : root_offset(-1) {
        file.open(DATA_FILE, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            // File doesn't exist, create it
            file.clear();
            file.open(DATA_FILE, std::ios::out | std::ios::binary);
            file.close();
            file.open(DATA_FILE, std::ios::in | std::ios::out | std::ios::binary);

            // Write initial header
            int header = -1; // No root
            file.write(reinterpret_cast<const char*>(&header), sizeof(int));
        } else {
            // Read root offset
            file.read(reinterpret_cast<char*>(&root_offset), sizeof(int));
        }
    }

    ~BPTree() {
        if (file.is_open()) {
            // Write root offset before closing
            file.seekp(0);
            file.write(reinterpret_cast<const char*>(&root_offset), sizeof(int));
            file.close();
        }
    }

    void insert(const std::string& key, int value) {
        if (root_offset == -1) {
            // Tree is empty, create root
            Node root;
            root.is_leaf = true;
            root.key_count = 1;
            root.keys[0] = key;
            root.values[0] = value;

            root_offset = sizeof(int); // After header
            write_node(root_offset, root);
            return;
        }

        Node root = read_node(root_offset);
        if (root.key_count == MAX_KEYS) {
            // Root is full, split it
            Node new_root;
            new_root.is_leaf = false;
            new_root.key_count = 0;
            new_root.children[0] = root_offset;

            int new_root_offset = file.tellp();

            // Split old root
            int new_child_offset = split_node(root_offset, root);
            Node new_child = read_node(new_child_offset);

            // Update new root
            new_root.keys[0] = new_child.keys[0];
            new_root.children[1] = new_child_offset;
            new_root.key_count = 1;

            root_offset = new_root_offset;
            write_node(root_offset, new_root);

            // Insert into appropriate child
            if (key < new_root.keys[0]) {
                insert_non_full(root_offset, new_root, key, value);
            } else {
                insert_non_full(new_child_offset, new_child, key, value);
            }
        } else {
            insert_non_full(root_offset, root, key, value);
        }
    }

    void remove(const std::string& key, int value) {
        // Find the leaf node containing the key-value pair
        int leaf_offset = find_leaf(key);
        if (leaf_offset == -1) return;

        Node leaf = read_node(leaf_offset);

        // Find and remove the key-value pair
        for (int i = 0; i < leaf.key_count; i++) {
            if (leaf.keys[i] == key && leaf.values[i] == value) {
                // Shift remaining elements
                for (int j = i; j < leaf.key_count - 1; j++) {
                    leaf.keys[j] = leaf.keys[j + 1];
                    leaf.values[j] = leaf.values[j + 1];
                }
                leaf.key_count--;
                write_node(leaf_offset, leaf);
                break;
            }
        }
    }

    std::vector<int> find(const std::string& key) {
        std::vector<int> result;

        int leaf_offset = find_leaf(key);
        if (leaf_offset == -1) return result;

        // Search through all leaf nodes that might contain this key
        int current_offset = leaf_offset;

        while (current_offset != -1) {
            Node current = read_node(current_offset);

            // Check if this node contains the key
            bool found_in_node = false;
            for (int i = 0; i < current.key_count; i++) {
                if (current.keys[i] == key) {
                    result.push_back(current.values[i]);
                    found_in_node = true;
                } else if (found_in_node && current.keys[i] > key) {
                    // Since keys are sorted, we can stop once we pass the key
                    break;
                }
            }

            // Move to next leaf node
            current_offset = current.next_leaf;

            // If we've passed all instances of the key, we can stop
            if (!found_in_node && !result.empty()) {
                break;
            }
        }

        // Sort values in ascending order
        std::sort(result.begin(), result.end());

        return result;
    }
};

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    BPTree tree;
    int n;
    std::cin >> n;
    std::cin.ignore();

    for (int i = 0; i < n; i++) {
        std::string command;
        std::cin >> command;

        if (command == "insert") {
            std::string key;
            int value;
            std::cin >> key >> value;
            tree.insert(key, value);
        } else if (command == "delete") {
            std::string key;
            int value;
            std::cin >> key >> value;
            tree.remove(key, value);
        } else if (command == "find") {
            std::string key;
            std::cin >> key;
            std::vector<int> values = tree.find(key);

            if (values.empty()) {
                std::cout << "null\n";
            } else {
                for (size_t j = 0; j < values.size(); j++) {
                    if (j > 0) std::cout << " ";
                    std::cout << values[j];
                }
                std::cout << "\n";
            }
        }
    }

    return 0;
}