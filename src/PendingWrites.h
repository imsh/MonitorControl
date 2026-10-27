#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

struct WriteKey
{
    size_t monitor = 0;
    uint8_t code = 0;   // VCP feature code

    auto operator<=>(const WriteKey&) const = default;
};

// Slider moves waiting to be sent. Only the latest value per (monitor, feature) matters,
// so a fast drag never builds up a backlog of slow DDC writes.
class PendingWrites
{
public:
    using Map = std::map<WriteKey, int>;

    void Set(WriteKey key, int percent) { values_[key] = percent; }
    [[nodiscard]] bool Empty() const { return values_.empty(); }
    Map TakeAll() { return std::exchange(values_, {}); }

private:
    Map values_;
};
