#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>

// Immutable snapshots share 16x16 horizontal pages. A write detaches only its page.
// World keys retain their existing encoding for export, picking and project history.
namespace wb {
template <class T> class VoxelPages {
    using Page = std::unordered_map<uint64_t, T>;
    using Index = std::unordered_map<int, std::shared_ptr<Page>>;
    Index pages_;
    size_t count_ = 0;
    uint32_t width_, depth_, stride_;
    int pageId(uint64_t key) const {
        const auto row = key / width_;
        return int((row % depth_) / 16 * stride_ + (key % width_) / 16);
    }
    static Page &writable(std::shared_ptr<Page> &p) {
        if (!p)
            p = std::make_shared<Page>();
        else if (p.use_count() != 1)
            p = std::make_shared<Page>(*p);
        return *p;
    }

  public:
    VoxelPages(int width, int depth) : width_(width), depth_(depth), stride_((width + 15) / 16) {}
    const Index &pages() const { return pages_; }
    const Page *page(int id) const {
        auto it = pages_.find(id);
        return it == pages_.end() ? nullptr : it->second.get();
    }
    size_t size() const { return count_; }
    bool empty() const { return !count_; }
    const T *get(uint64_t key) const {
        auto p = page(pageId(key));
        if (!p)
            return nullptr;
        auto it = p->find(key);
        return it == p->end() ? nullptr : &it->second;
    }
    bool contains(uint64_t key) const { return get(key) != nullptr; }
    const T &at(uint64_t key) const { return pages_.at(pageId(key))->at(key); }
    T &operator[](uint64_t key) {
        auto [it, added] = writable(pages_[pageId(key)]).try_emplace(key);
        count_ += added;
        return it->second;
    }
    void erase(uint64_t key) {
        auto it = pages_.find(pageId(key));
        if (it == pages_.end() || !it->second->contains(key))
            return;
        writable(it->second).erase(key);
        --count_;
        if (it->second->empty())
            pages_.erase(it);
    }
    bool operator==(const VoxelPages &o) const {
        if (count_ != o.count_ || pages_.size() != o.pages_.size())
            return false;
        for (const auto &[id, p] : pages_) {
            const auto peer = o.page(id);
            if (!peer || (peer != p.get() && *peer != *p))
                return false;
        }
        return true;
    }
    class Cursor {
        typename Index::const_iterator outer, last;
        typename Page::const_iterator inner;

      public:
        Cursor(typename Index::const_iterator i, typename Index::const_iterator end) : outer(i), last(end) {
            if (outer != last)
                inner = outer->second->begin();
        }
        const auto &operator*() const { return *inner; }
        Cursor &operator++() {
            if (++inner == outer->second->end() && ++outer != last)
                inner = outer->second->begin();
            return *this;
        }
        bool operator==(const Cursor &o) const {
            return outer == o.outer && (outer == last || inner == o.inner);
        }
    };
    Cursor begin() const { return {pages_.begin(), pages_.end()}; }
    Cursor end() const { return {pages_.end(), pages_.end()}; }
};
} // namespace wb
