#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>

namespace wb {
// Two-level copy-on-write page table. Indices remain the world's linear column
// indices, so rectangular worlds and legacy project coordinates do not change.
class HeightDeltas {
  public:
    static constexpr int PageColumns = 256, GroupPages = 64;
    struct Page {
        std::array<int, PageColumns> values{};
        int count = 0;
    };
    struct Group {
        std::map<int, std::shared_ptr<Page>> pages;
    };
    using Groups = std::map<int, std::shared_ptr<Group>>;
    const Groups &groups() const { return m_groups; }
    size_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }
    size_t pageCount() const { return m_pages; }
    int get(int index) const;
    void set(int index, int value);
    bool insert(int index, int value);
    uint64_t allocatedBytes() const;
    uint64_t uniqueBytes(std::set<const void *> &seen) const;
    bool operator==(const HeightDeltas &other) const;

    class const_iterator {
      public:
        std::pair<int, int> operator*() const;
        const_iterator &operator++();
        bool operator==(const const_iterator &other) const;

      private:
        friend class HeightDeltas;
        const HeightDeltas *owner = nullptr;
        Groups::const_iterator group;
        std::map<int, std::shared_ptr<Page>>::const_iterator page;
        int slot = 0;
        void skip();
    };
    const_iterator begin() const;
    const_iterator end() const;

  private:
    Groups m_groups;
    size_t m_size = 0, m_pages = 0;
};
} // namespace wb
