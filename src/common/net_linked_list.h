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
#ifndef HCOM_LINKEDLIST_H
#define HCOM_LINKEDLIST_H

namespace ock {
namespace hcom {
template <typename T> struct NetLinkedListNode {
    T data;
    NetLinkedListNode *prev;
    NetLinkedListNode *next;

    NetLinkedListNode()
    {
        data = T();
        ReLinkSelf();
    }

    void ReLinkSelf()
    {
        prev = this;
        next = this;
    }

    /*
     * @brief Insert node between prev and next
     */
    void InsertBetween(NetLinkedListNode<T> *prevNode, NetLinkedListNode<T> *nextNode)
    {
        if (NN_UNLIKELY(prevNode == nullptr || nextNode == nullptr)) {
            NN_LOG_ERROR("Invalid prevNode or nextNode");
            return;
        }
        nextNode->prev = this;
        this->next = nextNode;
        this->prev = prevNode;
        prevNode->next = this;
    }

    /*
     * @brief Remove self from linked list
     */
    void RemoveSelf()
    {
        if (next != nullptr) {
            next->prev = prev;
        }
        if (prev != nullptr) {
            prev->next = next;
        }
    }
};

template <typename T> struct NetLinkedList {
    NetLinkedListNode<T> head;

    NetLinkedList() = default;

    /*
     * @brief Link node to list's tail
     */
    void Append(NetLinkedListNode<T> *node)
    {
        if (NN_UNLIKELY(node == nullptr)) {
            NN_LOG_ERROR("Invalid node");
            return;
        }
        node->InsertBetween(head.prev, &head);
    }

    bool IsEmpty()
    {
        return head.next == &head;
    }
};
}
}
#endif // HCOM_LINKEDLIST_H
