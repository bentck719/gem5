#ifndef __MEM_GENERAL_QUEUE_HH__
#define __MEM_GENERAL_QUEUE_HH__

#include <list>
#include <unordered_map>
#include <optional>
#include <utility>

namespace gem5 
{
namespace memory 
{

template <typename KeyType, typename PayloadType>
class GeneralQueue {
private:
  size_t max_size_;
  std::list<std::pair<KeyType, PayloadType>> fifo_list_; // List 存 Key 和 Payload (Metadata)
  using ListIter = typename std::list<std::pair<KeyType, PayloadType>>::iterator; // Map 存 Key 對應到 List 的 iterator，加速查找
  std::unordered_map<KeyType, ListIter> map_; // 快速查找用

public:
  explicit GeneralQueue(size_t max_size) : max_size_(max_size) {}

  bool Contains(const KeyType &key) const { return map_.count(key); }
  bool IsFull() const { return map_.size() >= max_size_; }
  
  // 取得 Payload 指標 (Metadata)
  PayloadType* Get(const KeyType& key) {
    auto it = map_.find(key);
    if (it == map_.end()) return nullptr;
    return &(it->second->second);
  }

  std::optional<std::pair<KeyType, PayloadType>> Insert(const KeyType& key, const PayloadType& value) {
    if (Contains(key)) return std::nullopt;

    std::optional<std::pair<KeyType, PayloadType>> victim = std::nullopt;

    if (IsFull()) {
        victim = fifo_list_.front();
        PopFront(); 
    }

    // 3. 插入新資料
    fifo_list_.push_back({key, value});
    map_[key] = std::prev(fifo_list_.end());

    // 4. 回傳受害者
    return victim;
  }

  void PopFront() {
    if (fifo_list_.empty()) return;
    map_.erase(fifo_list_.front().first);
    fifo_list_.pop_front();
  }
  
  void Remove(const KeyType& key) {
    auto it = map_.find(key);
    if (it != map_.end()) {
        fifo_list_.erase(it->second);
        map_.erase(it);
    }
  }

  void Promote(const KeyType& key) {
    auto it = map_.find(key);
    if (it != map_.end()) {
        fifo_list_.splice(fifo_list_.end(), fifo_list_, it->second);
    }
  }
};

} // namespace memory
} // namespace gem5

#endif // __MEM_GENERAL_QUEUE_HH__