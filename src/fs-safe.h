#pragma once
// Directory loops that never throw (audit SEC-27). `for (auto& e : std::filesystem::directory_iterator(p, ec))`
// passes `ec` to the constructor only: the loop's ++ is the throwing overload, so an unreadable entry met while
// iterating (a locked sub-folder, a folder removed meanwhile) ends the process. FsSafe::Entries(p, ec) iterates
// with increment(ec): an error ends the loop and is left in `ec`.
#include <filesystem>
#include <system_error>

namespace FsSafe {

template <class It>
struct Range {
    It first;
    std::error_code* ec;
    struct iterator {
        It it;
        std::error_code* ec;
        const std::filesystem::directory_entry& operator*() const { return *it; }
        iterator& operator++() {
            it.increment(*ec);
            if (*ec) it = It();
            return *this;
        }
        bool operator!=(const iterator& o) const { return it != o.it; }
    };
    iterator begin() const { return {first, ec}; }
    iterator end() const { return {It(), ec}; }
};

inline Range<std::filesystem::directory_iterator> Entries(const std::filesystem::path& p, std::error_code& ec) {
    return {std::filesystem::directory_iterator(p, ec), &ec};
}
inline Range<std::filesystem::recursive_directory_iterator> EntriesRecursive(const std::filesystem::path& p, std::error_code& ec) {
    return {std::filesystem::recursive_directory_iterator(p, ec), &ec};
}

}  // namespace FsSafe
