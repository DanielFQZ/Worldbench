#include "HeightDeltas.h"
#include <cassert>

namespace wb {
int HeightDeltas::get(int index) const {
    if (index < 0)
        return 0;
    const int id = index / PageColumns;
    const auto group = m_groups.find(id / GroupPages);
    if (group == m_groups.end())
        return 0;
    const auto page = group->second->pages.find(id % GroupPages);
    return page == group->second->pages.end() ? 0 : page->second->values[index % PageColumns];
}
void HeightDeltas::set(int index, int value) {
    assert(index >= 0);
    const int old = get(index);
    if (old == value)
        return;
    const int id = index / PageColumns, groupId = id / GroupPages, pageId = id % GroupPages;
    auto &group = m_groups[groupId];
    if (!group)
        group = std::make_shared<Group>();
    else if (group.use_count() != 1)
        group = std::make_shared<Group>(*group);
    auto &page = group->pages[pageId];
    if (!page) {
        page = std::make_shared<Page>();
        ++m_pages;
    } else if (page.use_count() != 1)
        page = std::make_shared<Page>(*page);
    page->values[index % PageColumns] = value;
    if (!old) {
        ++page->count;
        ++m_size;
    }
    if (!value) {
        --page->count;
        --m_size;
    }
    if (!page->count) {
        group->pages.erase(pageId);
        --m_pages;
        if (group->pages.empty())
            m_groups.erase(groupId);
    }
}
bool HeightDeltas::insert(int index, int value) {
    if (index < 0 || value == 0 || get(index) != 0)
        return false;
    set(index, value);
    return true;
}
uint64_t HeightDeltas::allocatedBytes() const {
    return sizeof(*this) + m_groups.size() * (sizeof(Group) + 96) + m_pages * (sizeof(Page) + 96);
}
uint64_t HeightDeltas::uniqueBytes(std::set<const void *> &seen) const {
    if (!seen.insert(this).second)
        return 0;
    uint64_t bytes = sizeof(*this) + m_groups.size() * 64;
    for (const auto &[id, group] : m_groups)
        if (seen.insert(group.get()).second) {
            bytes += sizeof(Group) + 32 + group->pages.size() * 64;
            for (const auto &[pageId, page] : group->pages)
                if (seen.insert(page.get()).second)
                    bytes += sizeof(Page) + 32;
        }
    return bytes;
}
void HeightDeltas::const_iterator::skip() {
    while (group != owner->m_groups.end()) {
        while (page != group->second->pages.end()) {
            while (slot < PageColumns) {
                if (page->second->values[slot])
                    return;
                ++slot;
            }
            ++page;
            slot = 0;
        }
        ++group;
        if (group != owner->m_groups.end())
            page = group->second->pages.begin();
    }
}
std::pair<int, int> HeightDeltas::const_iterator::operator*() const {
    return {(group->first * GroupPages + page->first) * PageColumns + slot, page->second->values[slot]};
}
HeightDeltas::const_iterator &HeightDeltas::const_iterator::operator++() {
    ++slot;
    skip();
    return *this;
}
bool HeightDeltas::const_iterator::operator==(const const_iterator &other) const {
    return owner == other.owner && group == other.group &&
           (group == owner->m_groups.end() || (page == other.page && slot == other.slot));
}
HeightDeltas::const_iterator HeightDeltas::begin() const {
    const_iterator it;
    it.owner = this;
    it.group = m_groups.begin();
    if (it.group != m_groups.end())
        it.page = it.group->second->pages.begin();
    it.skip();
    return it;
}
HeightDeltas::const_iterator HeightDeltas::end() const {
    const_iterator it;
    it.owner = this;
    it.group = m_groups.end();
    return it;
}
bool HeightDeltas::operator==(const HeightDeltas &other) const {
    if (m_size != other.m_size)
        return false;
    auto a = begin(), b = other.begin();
    for (; a != end(); ++a, ++b)
        if (*a != *b)
            return false;
    return true;
}
} // namespace wb
