#include <sys/uio.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <array>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <ctre.hpp>

using namespace ctre::literals;

constexpr static auto RC4_TABLE_SIZE                = 256;
constexpr static auto RC4_INVALID_MASK_SHOCKWAVE    = 0xFFFFFFFB'FFFFFF00;
constexpr static auto RC4_INVALID_MASK_FLASH        = 0xFFFFFF00;

template<auto size>
struct rc4_entry_raw {
    static_assert(size == 8 || size == 4);
 
    const std::uint8_t raw[size];

    constexpr inline auto table_entry() const
    {
        return raw[0];
    }

    constexpr inline auto fits_mask() const
    {
        if constexpr (size == 4) {
            return (*reinterpret_cast<const std::uint32_t*>(&raw) & RC4_INVALID_MASK_FLASH) == 0;
        }

        if constexpr (size == 8) {
            return (*reinterpret_cast<const std::uint64_t*>(&raw) & RC4_INVALID_MASK_SHOCKWAVE) == 0;
        }

        std::unreachable();
    }
};

static_assert(sizeof(rc4_entry_raw<4>) == 4);
static_assert(sizeof(rc4_entry_raw<8>) == 8);

template<auto sz> using rc4_table_raw = std::array<rc4_entry_raw<sz>, RC4_TABLE_SIZE>;

using rc4_table = std::array<std::uint8_t, RC4_TABLE_SIZE>;

struct map
{
    std::uint64_t start;
    std::uint64_t end;

    std::uint64_t size() const { return end - start; }
};

std::vector<map> get_process_maps(pid_t pid) noexcept
{
    std::vector<map> maps;

    auto process_maps_file = std::ifstream{ std::format("/proc/{}/maps", pid) };
    std::string current_line;

    while (std::getline(process_maps_file, current_line))
    {
        if (const auto [matches, start_view, end_view] = ctre::match<"([A-Fa-f0-9]+)-([A-Fa-f0-9]+)\\s.+">(current_line); matches)
        {
            const auto start = std::string(start_view);
            const auto end = std::string(end_view);

            // fprintf(stderr, "%s - %s\t", start.c_str(), end.c_str());

            const auto m = map {
                .start = std::strtoull(start.c_str(), nullptr, 16),
                .end = std::strtoull(end.c_str(), nullptr, 16),
            };

            // fprintf(stderr, "%08x%08x - %08x%08x\n", static_cast<std::uint32_t>(m.start >> 32), static_cast<std::uint32_t>(m.start),
            //                                          static_cast<std::uint32_t>(m.end >> 32), static_cast<std::uint32_t>(m.end));

            // if (m.size() > 4 * 1024 * 1024)
            //     continue;

            maps.push_back(m);
        }
    }

    return maps;
}

template<auto sz>
constexpr std::optional<rc4_table> extract_table(const rc4_table_raw<sz>& raw_table) noexcept
{
    auto table = rc4_table{ 0 };
    auto values_already_seen = std::array<bool, RC4_TABLE_SIZE>{};
    values_already_seen.fill(false);

    for(auto i = 0uz; i < raw_table.size(); i++)
    {
        const auto extracted_value = raw_table[i].table_entry();

        if (values_already_seen[extracted_value])
            return {};

        table[i] = extracted_value;
        values_already_seen[extracted_value] = true;
    }

    return table;
}

template<auto sz>
constexpr std::vector<rc4_table> check_map_tables(const std::span<const rc4_entry_raw<sz>> buffer) noexcept
{
    std::vector<rc4_table> tables;

    if (buffer.size() < RC4_TABLE_SIZE)
        return tables;

    for(auto it = buffer.begin(); it <= buffer.end() - RC4_TABLE_SIZE; it++)
    {
        const auto& current_table = reinterpret_cast<const rc4_table_raw<sz>&>(*it);

        auto is_good = true;
        for(auto i = 0uz; i < current_table.size(); i++)
        {
            const auto& entry = current_table[i];

            if (!entry.fits_mask()) {
                is_good = false;
                it += static_cast<long>(i);
                break;
            }
        }

        if (!is_good)
            continue;

        const auto maybe_table = extract_table(current_table);
        if (maybe_table.has_value()) {
            tables.push_back(maybe_table.value());
        }
    }

    return tables;
}

void print_table(const rc4_table& table) noexcept
{
    for(const auto entry : table)
        printf("%02x", entry);

    printf("\n");
}

template<auto sz>
void check_and_print_tables_with_offset(const auto& buffer, const std::size_t offset) noexcept
{
    const auto span = std::span<const rc4_entry_raw<sz>> { 
        reinterpret_cast<const rc4_entry_raw<sz>*>(reinterpret_cast<std::uint64_t>(buffer.data()) + offset), 
        (buffer.size() - offset) / sz 
    };

    for(const auto& table : check_map_tables<sz>(span))
        print_table(table);
};

void check_map(const pid_t pid, const map m) noexcept
{
    std::vector<std::uint8_t> buffer;
    buffer.resize(m.size());

    iovec local{ .iov_base = buffer.data(), .iov_len = m.size() };
    iovec remote{ .iov_base = reinterpret_cast<void*>(m.start), .iov_len = m.size() };

    const auto rc = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    if (rc < 0)
        return;

    check_and_print_tables_with_offset<8>(buffer, 0);
    check_and_print_tables_with_offset<8>(buffer, 4);

    // flash:
    // check_and_print_tables_with_offset<4>(buffer, 0);
}

int main(int argc, const char** argv)
{
    const auto pid = pid_t{ std::atoi(argv[1]) };
    const auto maps = get_process_maps(pid);

    fprintf(stderr, "number of maps: %lu\n", maps.size());

    for(const auto m : maps)
        check_map(pid, m);

    fputs("Done", stderr);

    return 0;
}


