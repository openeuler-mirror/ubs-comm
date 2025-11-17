/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <thread>
#include "hcom_def.h"
#include "common/net_rb_tree.h"
#include "hcom_utils.h"
#include "test_net_rbtree.h"

using namespace ock::hcom;

TestNetRbTree::TestNetRbTree() {}

void TestNetRbTree::SetUp() {}

void TestNetRbTree::TearDown() {}

static void InsertToRbTree(NetRbTree<int> *tree, NetRbNode<int> *node)
{
    auto cur = &tree->ref;
    NetRbNode<int> *parent = nullptr;
    while (*cur != nullptr) {
        parent = *cur;
        if (node->data < (*cur)->data) {
            cur = &(parent->left);
        } else if (node->data > (*cur)->data) {
            cur = &(parent->right);
        } else {
            return;
        }
    }
    node->Link(parent, cur);
    tree->Insert(node);
}

static void InsertToRbTree(NetRbTree<int> *tree, int val)
{
    InsertToRbTree(tree, new NetRbNode<int>(val));
}

static void CppErase(NetRbTree<int> *tree, int val)
{
    auto cur = tree->ref;
    NetRbNode<int> *target = nullptr;
    while (cur) {
        if (cur->data == val) {
            target = cur;
            break;
        } else if (cur->data > val) {
            cur = cur->left;
        } else {
            cur = cur->right;
        }
    }
    tree->Erase(target);
}


static void LevelOrderTraverseDump(NetRbTree<int> *tree)
{
    std::cout << "===========================dump rbtree===========================" << std::endl;
    std::vector<NetRbNode<int> *> seq { tree->ref };
    std::vector<int> ret;
    while (!seq.empty()) {
        std::vector<NetRbNode<int> *> newSeq;
        for (uint i = 0; i < seq.size(); ++i) {
            if (!seq[i]) {
                std::cout << "null ";
                continue;
            }
            ret.push_back(seq[i]->data);
            std::cout << seq[i]->data << " ";
            newSeq.push_back(seq[i]->left);
            newSeq.push_back(seq[i]->right);
        }
        seq = newSeq;
        std::cout << std::endl;
    }
}

static bool IsEveryRedNodeHasTwoBlackChildren(NetRbNode<int> *node)
{
    if (!node) {
        return true;
    }
    if (node->IsRed()) {
        if (node->left == nullptr && node->right == nullptr) {
            return true;
        } else if (node->left == nullptr || node->right == nullptr) {
            return false;
        } else if (node->left->IsRed() || node->right->IsRed()) {
            return false;
        }
    }

    if (node->left && !IsEveryRedNodeHasTwoBlackChildren(node->left)) {
        return false;
    }
    if (node->right && !IsEveryRedNodeHasTwoBlackChildren(node->right)) {
        return false;
    }
    return true;
}

static std::pair<int, bool> ChildrenHasSameBlackHeight(NetRbNode<int> *node)
{
    if (!node) {
        return { 0, true };
    }
    int leftBH = node->IsBlack() ? 1 : 0;
    int rightBH = node->IsBlack() ? 1 : 0;
    if (node->left) {
        auto ret = ChildrenHasSameBlackHeight(node->left);
        if (!ret.second) {
            return { 0, false };
        }
        leftBH += ret.first;
    }
    if (node->right) {
        auto ret = ChildrenHasSameBlackHeight(node->left);
        if (!ret.second) {
            return { 0, false };
        }
        rightBH += ret.first;
    }
    return { leftBH, leftBH == rightBH };
}

static bool IsInorderAscend(NetRbNode<int> *node)
{
    if (!node) {
        return true;
    }

    if (node->left) {
        if (node->data < node->left->data) {
            return false;
        }
        if (!IsInorderAscend(node->left)) {
            return false;
        }
    }

    if (node->right) {
        if (node->data > node->right->data) {
            return false;
        }
        if (!IsInorderAscend(node->right)) {
            return false;
        }
    }

    return true;
}

static bool IsValidRBTree(NetRbTree<int> *tree)
{
    if (!tree || !tree->ref) {
        return true;
    }

    /* law1:node must be black or red, no need to check for only 1 bit present for color
     * law3:every leaf is black, for we just use nullptr for leaf,do not count it for bh,no need to check
     * law3:black root */
    if (tree->ref->IsRed()) {
        std::cout << "Invalid RBTree, Red Root" << std::endl;
        return false;
    }

    /* law4:every red node must has two black children or no child */
    if (!IsEveryRedNodeHasTwoBlackChildren(tree->ref)) {
        std::cout << "Invalid RBTree, Red Parent Has Red Child or Single Black Child" << std::endl;
        return false;
    }

    /* law5:every path from one node has same black height */
    if (!ChildrenHasSameBlackHeight(tree->ref).second) {
        std::cout << "Invalid RBTree, BH is not balance" << std::endl;
        return false;
    }

    /* data check:inorder ascend */
    if (!IsInorderAscend(tree->ref)) {
        std::cout << "Invalid RBTree, wrong data order" << std::endl;
        return false;
    }

    return true;
}

TEST_F(TestNetRbTree, Serial)
{
    NetRbTree<int> rbTree;

    std::set<int> seeds;
    std::set<int> delSeeds;
    uint64_t totalTime = 0;
    for (int k = 0; k < 10; ++k) {
        seeds.clear();
        delSeeds.clear();

        for (int i = 0; i < 10000; ++i) {
            auto v = random() % 10000;
            seeds.insert(v);
            if (v % 3 == 1) {
                delSeeds.insert(v);
            }
        }
        auto cost = MONOTONIC_TIME_NS();
        for (const auto &item : seeds) {
            InsertToRbTree(&rbTree, item);
        }
        cost = MONOTONIC_TIME_NS() - cost;
        totalTime += cost;
        auto ret = IsValidRBTree(&rbTree);

        ASSERT_EQ(ret, true);

        for (const auto &item : delSeeds) {
            CppErase(&rbTree, item);
        }

        ret = IsValidRBTree(&rbTree);

        ASSERT_EQ(ret, true);
    }

    std::cout << "RbTree insert avg cost:" << (totalTime / 10 / 10000) << "ns" << std::endl;
}

TEST_F(TestNetRbTree, EraseColorLeft)
{
    NetRbTree<int> rbTree;
    NetRbNode<int> *root = new NetRbNode<int>(0);
    InsertToRbTree(&rbTree, root);

    NetRbNode<int> *node = new NetRbNode<int>(2);
    NetRbNode<int> *parent = new NetRbNode<int>(0);
    parent->right = nullptr;

    ASSERT_EQ(rbTree.EraseColorLeft(node, parent), false);

    delete node;
    delete parent;
    delete root;
}

TEST_F(TestNetRbTree, EraseColorRight)
{
    NetRbTree<int> rbTree;
    NetRbNode<int> *root = new NetRbNode<int>(0);
    InsertToRbTree(&rbTree, root);

    NetRbNode<int> *node = new NetRbNode<int>(2);
    NetRbNode<int> *parent = new NetRbNode<int>(0);
    parent->left = nullptr;

    ASSERT_EQ(rbTree.EraseColorRight(node, parent), false);

    delete node;
    delete parent;
    delete root;
}