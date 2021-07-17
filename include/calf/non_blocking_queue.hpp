#ifndef CALF_PLATFORM_WINDOWS_QUEUE_H_
#define CALF_PLATFORM_WINDOWS_QUEUE_H_

#include "common.h"
#include "debug.h"

#include <calf/meta/type_traits.h>

namespace calf {
namespace platform {
namespace windows {

// 线程安全队列，Free-Lock 单向链表实现。
template<typename Data>
class Queue {
public:
  struct Node {
    template<typename ...Args>
    Node(Args&&... args) : data(meta::KeepReference(args)...), next(nullptr) {}

    Data data;
    volatile Node* next;
  };

public:
  Queue() : head_(nullptr), tail_(nullptr) {}

  // 向尾部追加节点。
  void Push(Node* node) {
    // 需要支持多线程操作，这里使用 CAS 确保线程安全。
    Node* current_tail = nullptr;
    Node* result = nullptr;

    do {
      // 缓存当前的队尾和后置节点。
      current_tail = tail_;

      // 边界判断，检查是否是第一个节点
      if (current_tail == nullptr) {
        debug::Assert(head_ == nullptr);

        // 尝试添加第一个节点
        result = ::_InterlockedCompareExchangePointer(&tail_, nullptr, node);
        if (current_tail != result) {
          // 添加成功，更新队头
          ::_InterlockedExchangePointer(&head_, node);
        }
      } else {
        Node* current_next = current_next->next;

        // 检查置尾节点
        // 如果后置节点不为空，说明队尾需要更新。
        // 这步操作是为了保证，在其它线程没有正确更新队尾的情况下，下一步 CAS 操作不会被锁死。
        if (current_next != nullptr) {
          ::_InterlockedCompareExchangePointer(&tail_, current_tail, current_next);
          continue;
        }

        // 尝试添加尾节点
        result = ::_InterlockedCompareExchangePointer(&current_tail->next, current_next, node);
      }

      // 如果添加失败，就重试。
    } while(result != node);

    // 添加成功，置尾节点
    // 这里失败了也没关系，可能被其它线程抢先了。
    ::_InterlockedCompareExchangePointer(&tail_, current_tail, node);
  }

  Node* Pop() {
    Node* result = nullptr;

    do {
      Node* current_head = head_;
      Node* current_tail = tail_;

      if (current_head == nullptr) {
        // 空队列，直接返回。
        break;
      } else {
        Node* current_next = current_head->next;

        // 检查置尾节点
        if (current_next != nullptr) {
          ::_InterlockedCompareExchangePointer(&tail_, current_tail, current_next);
          continue;
        }

        // 准备删除队头节点
        // 如果要删除的节点同时是尾节点，先把尾节点删掉。防止被其它线程操作
        if (current_head == current_tail) {
          result = ::_InterlockedCompareExchangePointer(&tail_, current_tail, nullptr);
          if (result != nullptr) {
            // 删除失败，重试
            continue;
          }

          

          // 删除头节点，这里允许失败，确保尾节点删除即可。
          ::_InterlockedCompareExchangePointer(&head_, current_head, nullptr);
        } else {
          // 准备删除头节点
          result = ::_InterlockedCompareExchangePointer(&head_, current_head, current_next);
          if (result != current_head) {
            // 头结点删除失败，重试
            continue;
          }
        }

        result = current_head;
      }

      // 失败重试
    } while(result == nullptr);

    return result;
  }

private:
  volatile Node* head_;
  volatile Node* tail_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_QUEUE_H_