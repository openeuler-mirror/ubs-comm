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
#ifndef LOCKFREES_RBTREEWRAPPER_H
#define LOCKFREES_RBTREEWRAPPER_H

namespace ock {
namespace hcom {
enum NetRbColor : uint8_t {
    RB_RED,
    RB_BLACK
};

/*
 * @brief red-black tree node
 * @tparam T data type stored in tree node
 */
template <typename T> struct NetRbNode {
    T data;

    uint64_t rbParentColor = 0;
    NetRbNode<T> *left = nullptr;
    NetRbNode<T> *right = nullptr;

    NetRbNode()
    {
        data = T();
    }

    explicit NetRbNode(T _data) : data(_data)
    {
        ClearParent();
    }

    inline NetRbNode<T> *GetParent() const
    {
        return reinterpret_cast<NetRbNode<T> *>(rbParentColor & ~0x3);
    }

    inline NetRbColor GetColor() const
    {
        return static_cast<NetRbColor>(rbParentColor & 1);
    }

    inline bool IsBlack() const
    {
        return GetColor() == RB_BLACK;
    }

    inline bool IsRed() const
    {
        return GetColor() == RB_RED;
    }

    inline void SetBlack()
    {
        rbParentColor |= RB_BLACK;
    }

    inline void SetRed()
    {
        rbParentColor &= ~1;
    }

    inline void SetParent(NetRbNode<T> *parent)
    {
        rbParentColor = (rbParentColor & 3UL) | reinterpret_cast<uint64_t>(parent);
    }

    inline void SetColor(uint64_t color)
    {
        rbParentColor = (rbParentColor & ~1) | color;
    }

    inline bool IsOrphan() const
    {
        return GetParent() == this;
    }

    inline void ClearParent()
    {
        SetParent(this);
    }

    /*
     * @brief Get prev node according to inorder traverse
     */
    inline NetRbNode *Prev()
    {
        if (this->GetParent() == this) {
            return nullptr;
        }

        auto node = this;

        if (node->left) {
            node = node->left;

            while (node->right) {
                node = node->right;
            }
        }

        return node;
    }

    /*
     * @brief Get successor node according to inorder traverse
     */
    inline NetRbNode<T> *Next()
    {
        NetRbNode<T> *parent = nullptr;

        if (IsOrphan()) {
            return nullptr;
        }

        auto node = this;

        if (node->right) {
            node = node->right;

            while (node->left) {
                node = node->left;
            }

            return node;
        }

        while ((parent = node->GetParent()) && node == parent->right) {
            node = parent;
        }

        return parent;
    }

    /*
     * @brief Link current node to parent
     */
    inline void Link(NetRbNode<T> *parent, NetRbNode<T> **link)
    {
        if (NN_UNLIKELY(link == nullptr)) {
            return;
        }
        rbParentColor = reinterpret_cast<uint64_t>(parent);
        left = nullptr;
        right = nullptr;
        *link = this;
    }
} __attribute__((aligned(sizeof(long))));

template <typename T> struct NetRbTree {
    NetRbNode<T> *ref = nullptr;

    inline void RotateLeft(NetRbNode<T> *node)
    {
        if (NN_UNLIKELY(node == nullptr)) {
            return;
        }
        auto right = node->right;
        auto parent = node->GetParent();

        if ((node->right = right->left)) {
            right->left->SetParent(node);
        }

        right->left = node;
        right->SetParent(parent);

        if (parent) {
            if (node == parent->left) {
                parent->left = right;
            } else {
                parent->right = right;
            }
        } else {
            ref = right;
        }
        node->SetParent(right);
    }

    inline void RotateRight(NetRbNode<T> *node)
    {
        if (NN_UNLIKELY(node == nullptr)) {
            return;
        }
        auto left = node->left;
        auto parent = node->GetParent();

        if ((node->left = left->right)) {
            left->right->SetParent(node);
        }

        left->right = node;
        left->SetParent(parent);

        if (parent) {
            if (node == parent->right) {
                parent->right = left;
            } else {
                parent->left = left;
            }
        } else {
            ref = left;
        }

        node->SetParent(left);
    }

    /*
     * @brief subroutine of Insert when the node to insert is on left side of parent
     */
    inline bool InsertLeft(NetRbNode<T> *&node, NetRbNode<T> *&parent, NetRbNode<T> *&gparent)
    {
        if (NN_UNLIKELY(parent == nullptr || gparent == nullptr)) {
            return false;
        }
        NetRbNode<T> *uncle = gparent->right;

        /* if both parent and uncle is red, grandparent must be blacked,we just
         * transfer grandparent's black to parent and uncle, then go upwards to
         * check grandparent further more */
        if (uncle && uncle->IsRed()) {
            uncle->SetBlack();
            parent->SetBlack();
            gparent->SetRed();
            node = gparent;
            return true;
        }

        /* uncle is black,only recoloring will cause uncle lose 1 bh, which means bh imbalance,
         * we try to make node's position to parent is same as parent's position to grandparent
         * in current branch, parent is left child,node is right child,we call this LR type,
         * we left-rotate parent to exchange node and parent, which result in
         * both node and parent is their parents' left child, which called LL type
         * LL type(same as RR type in symmetry situation) structure is easier to rebalance */
        if (parent->right == node) {
            NetRbNode<T> *tmp = nullptr;
            RotateLeft(parent);
            tmp = parent;
            parent = node;
            node = tmp;
        }

        /* now the type is LL, we push down the grandparent's black to parent and rotate parent to be
         * grandparent, balancing finished */
        parent->SetBlack();
        gparent->SetRed();
        RotateRight(gparent);
        return false;
    }

    inline bool InsertRight(NetRbNode<T> *&node, NetRbNode<T> *&parent, NetRbNode<T> *&gparent)
    {
        if (NN_UNLIKELY(parent == nullptr || gparent == nullptr)) {
            return false;
        }
        NetRbNode<T> *uncle = gparent->left;

        if (uncle && uncle->IsRed()) {
            uncle->SetBlack();
            parent->SetBlack();
            gparent->SetRed();
            node = gparent;
            return true;
        }

        if (parent->left == node) {
            NetRbNode<T> *tmp = nullptr;
            RotateRight(parent);
            tmp = parent;
            parent = node;
            node = tmp;
        }

        parent->SetBlack();
        gparent->SetRed();
        RotateLeft(gparent);
        return false;
    }

    /*
     * @brief insert node to rbt,this routine does not include searching process,before calling this routine,
     * caller needs to search the correct parent, and call Link to link node to its parent
     *
     * @param node
     */
    inline void Insert(NetRbNode<T> *node)
    {
        if (NN_UNLIKELY(node == nullptr)) {
            return;
        }
        NetRbNode<T> *parent = nullptr;
        NetRbNode<T> *gparent = nullptr;

        /* go upwards until there is no continuous red child & parent, since parent
         * is red, grandparent must be black */
        while ((parent = node->GetParent()) && parent->IsRed()) {
            gparent = parent->GetParent();
            if (parent == gparent->left) {
                if (InsertLeft(node, parent, gparent)) {
                    continue;
                }
            } else {
                if (InsertRight(node, parent, gparent)) {
                    continue;
                }
            }
        }
        if (ref == nullptr) {
            return;
        }
        ref->SetBlack();
    }

    /*
     * @brief subroutine of EraseColor when the node is on left side of parent
     */
    inline bool EraseColorLeft(NetRbNode<T> *&node, NetRbNode<T> *&parent)
    {
        if (NN_UNLIKELY(parent == nullptr)) {
            return false;
        }
        auto other = parent->right;

        if (NN_UNLIKELY(other == nullptr)) {
            return false;
        }

        /* as sibling is red, we get Rr__ type,which we can convert to Rb__,
         * just black sibling, red parent and left rotate parent */
        if (other->IsRed()) {
            other->SetBlack();
            parent->SetRed();
            RotateLeft(parent);
            other = parent->right;
        }

        /* now, the type must be Rb__, then we determine nephew type furtherly,
         * no children or only has black children,the final type is Rb,just red sibling and go upwards */
        if ((!other->left || other->left->IsBlack()) && (!other->right || other->right->IsBlack())) {
            other->SetRed();
            node = parent;
            parent = node->GetParent();
        } else {
            /* no red right nephew, type is RbLr,do red sibling,black left nephew and
             * left rotate sibling, then update sibling, thus type converted to RbRr */
            if (!other->right || other->right->IsBlack()) {
                other->left->SetBlack();
                other->SetRed();
                RotateRight(other);
                other = parent->right;
            }

            /* type RbRr, we recompense deleted bh 1 to current left path by blacking and rotating parent to
             * left side, which may result in lacking bh 1 for right path when parent is already blacked, so
             * we also color sibling by parent's color,till now,the visited subtree has same bh and root
             * color as before this routine,no need for further upward checking,just break */
            other->SetColor(parent->GetColor());
            parent->SetBlack();
            if (NN_UNLIKELY(!other->right)) {
                return false;
            }
            other->right->SetBlack();
            RotateLeft(parent);
            node = ref;
            return true;
        }
        return false;
    }

    inline bool EraseColorRight(NetRbNode<T> *&node, NetRbNode<T> *&parent)
    {
        if (NN_UNLIKELY(parent == nullptr)) {
            return false;
        }
        auto other = parent->left;

        if (NN_UNLIKELY(other == nullptr)) {
            return false;
        }

        if (other->IsRed()) {
            other->SetBlack();
            parent->SetRed();
            RotateRight(parent);
            other = parent->left;
        }

        if ((!other->left || other->left->IsBlack()) && (!other->right || other->right->IsBlack())) {
            other->SetRed();
            node = parent;
            parent = node->GetParent();
        } else {
            if (!other->left || other->left->IsBlack()) {
                other->right->SetBlack();
                other->SetRed();
                RotateLeft(other);
                other = parent->left;
            }

            other->SetColor(parent->GetColor());
            parent->SetBlack();
            if (NN_UNLIKELY(!other->left)) {
                return false;
            }
            other->left->SetBlack();
            RotateRight(parent);
            node = ref;
            return true;
        }
        return false;
    }

    /*
     * @brief rebalancing red-black tree after deleting black node
     * @param node deleted node
     * @param parent parent of deleted node
     *
     * Only take deleting left son in account
     * For the rebalancing logic is symmetrically identical to another side
     * Def. Only 4 types satify rbt laws and affect rebalancing process
     * _1_2_3_4 _1 stands for sibling side,_2 stands for sibling color,_3 stands for nephew side,_4 stands for nephew
     * color Notice! absent of _3 and _4 means sibling has no children or every child is black The 4 types are
     * Rb/Rr/RbLr/RbRr,Rr(__) can be converted to Rb(__),which is simpler to rebalance, the same as converting RbLr to
     * RbRr
     */
    inline void EraseColor(NetRbNode<T> *node, NetRbNode<T> *parent)
    {
        /* rebalancing upwards until node reached root or became red */
        while ((node == nullptr || node->IsBlack()) && node != ref) {
            if (NN_UNLIKELY(parent == nullptr)) {
                return;
            }
            if (parent->left == node) {
                if (EraseColorLeft(node, parent)) {
                    break;
                }
            } else {
                if (EraseColorRight(node, parent)) {
                    break;
                }
            }
        }

        /* In few cases we can get an node to receive the deleted black color,
         * then we just black it
         * this operation may be invalid when node is already blacked,
         * which means we reached root node,also means we complete rebalance, so
         * this invalid blacking do no harm */
        if (node) {
            node->SetBlack();
        }
    }

    /*
     * @brief subroutine of Erase when node has two children
     */
    inline void EraseWithTwoChildren(NetRbNode<T> *&node, NetRbNode<T> *&parent, NetRbNode<T> *&child,
        NetRbColor &color)
    {
        NetRbNode<T> *old = node;
        NetRbNode<T> *left = nullptr;

        node = node->right;
        while ((left = node->left) != nullptr) {
            node = left;
        }

        parent = old->GetParent();
        if (parent) {
            if (parent->left == old) {
                parent->left = node;
            } else {
                parent->right = node;
            }
        } else {
            ref = node;
        }

        child = node->right;
        parent = node->GetParent();
        color = node->GetColor();

        if (parent == old) {
            parent = node;
        } else {
            if (child) {
                child->SetParent(parent);
            }

            parent->left = child;
            node->right = old->right;
            old->right->SetParent(node);
        }

        node->rbParentColor = old->rbParentColor;
        node->left = old->left;
        old->left->SetParent(node);
    }

    /*
     * @brief delete node from rbt,the process is same as binary search tree deleting,
     * except we may do some recoloring and rotation later
     *
     * @param node the node to delete
     */
    inline void Erase(NetRbNode<T> *node)
    {
        if (NN_UNLIKELY(node == nullptr)) {
            return;
        }
        NetRbNode<T> *child = nullptr;
        NetRbNode<T> *parent = nullptr;
        NetRbColor color;

        if (!node->left) {
            child = node->right;
        } else if (!node->right) {
            child = node->left;
        } else {
            EraseWithTwoChildren(node, parent, child, color);

            // fix: coloring label is at bottom of the routine
            if (color == RB_BLACK) {
                EraseColor(child, parent);
            }
            return;
        }

        parent = node->GetParent();
        color = node->GetColor();

        if (child) {
            child->SetParent(parent);
        }

        if (parent) {
            if (parent->left == node) {
                parent->left = child;
            } else {
                parent->right = child;
            }
        } else {
            ref = child;
        }

        /* only delete black node corrupt rbt,we need do rebalancing */
        if (color == RB_BLACK) {
            EraseColor(child, parent);
        }
    }

    /*
     * @param victim the node to be replaced
     * @param newNode the node to replace victim
     */
    inline void Replace(NetRbNode<T> *victim, NetRbNode<T> *newNode)
    {
        if (NN_UNLIKELY(victim == nullptr || newNode == nullptr)) {
            return;
        }
        auto parent = victim->GetParent();
        if (parent) {
            if (victim == parent->left) {
                parent->left = newNode;
            } else {
                parent->right = newNode;
            }
        } else {
            ref = newNode;
        }

        if (victim->left) {
            victim->left->SetParent(newNode);
        }

        if (victim->right) {
            victim->right->SetParent(newNode);
        }

        newNode->rbParentColor = victim->rbParentColor;
        newNode->left = victim->left;
        newNode->right = victim->right;
    }
};
}
}
#endif // LOCKFREES_RBTREEWRAPPER_H
