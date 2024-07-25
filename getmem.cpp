
#include <sys/uio.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <span>
#include <array>
#include <vector>
#include <fstream>
#include <string>
#include <format>
#include <optional>

#include <ctre.hpp>

using namespace ctre::literals;

constexpr static auto RC4_TABLE_SIZE = 256;
constexpr static auto RC4_INVALID_MASK_SHOCKWAVE = 0xFFFFFFFB'FFFFFF00;

struct map
{
    std::uint64_t start;
    std::uint64_t end;

    std::uint64_t size() const { return end - start; }
};

struct rc4_entry_raw
{
    union {
        std::uint64_t big;
        std::uint32_t medium[2];
        std::uint8_t small[8];
    } value;

};

static_assert(sizeof(rc4_entry_raw) == 8);

using rc4_table = std::array<std::uint8_t, RC4_TABLE_SIZE>;
using rc4_table_raw = std::array<rc4_entry_raw, RC4_TABLE_SIZE>;

std::vector<map> get_process_maps(pid_t pid)
{
    std::vector<map> maps;

    auto process_maps_file = std::ifstream{ std::format("/proc/{}/maps", pid) };
    std::string current_line;

    while (std::getline(process_maps_file, current_line))
    {
        if (const auto [matches, start_view, end_view] = 
            ctre::match<"([A-Fa-f0-9]+)-([A-Fa-f0-9]+)\\s.+">(current_line); matches)
        {
            const auto start = std::string(start_view);
            const auto end = std::string(end_view);

            const auto m = map { 
                .start = std::strtoull(start.c_str(), nullptr, 16),
                .end = std::strtoull(end.c_str(), nullptr, 16),
            };

            // if (m.size() > 4 * 1024 * 1024)
            //     continue;

            maps.push_back(m);
        }
    }

    return maps;
}

void print_table(const rc4_table& table)
{
    for(const auto entry : table)
        printf("%02x", entry);

    printf("\n");
}

constexpr std::optional<rc4_table> extract_table(const rc4_table_raw& raw_table)
{
    auto table = rc4_table{ 0 };
    auto values_already_seen = std::array<bool, RC4_TABLE_SIZE>{ false };

    for(auto i = 0uz; i < raw_table.size(); i++)
    {
        const auto extracted_value = raw_table[i].value.small[0];
        if (values_already_seen[extracted_value])
            return {};

        table[i] = extracted_value;
        values_already_seen[extracted_value] = true;
    }

    return table;
}

constexpr std::vector<rc4_table> check_map_tables(const std::span<const rc4_entry_raw> buffer)
{
    std::vector<rc4_table> tables;

    if (buffer.size() < RC4_TABLE_SIZE)
        return tables;

    auto start = buffer.begin();

    for(auto it = buffer.begin(); it != buffer.end(); it++)
    {
        const auto& entry = *it;

        if ((entry.value.big & RC4_INVALID_MASK_SHOCKWAVE) != 0) {
            start = it + 1;
        }

        else if (it - start == RC4_TABLE_SIZE) {
            const auto& current_table = reinterpret_cast<const rc4_table_raw&>(*start);
            const auto maybe_table = extract_table(current_table);
            if (maybe_table.has_value()) {
                tables.push_back(maybe_table.value());
            }
            start++;
        }
    }

    return tables;
}

void check_map(const pid_t pid, const map m)
{
    std::vector<std::uint8_t> buffer;
    buffer.resize(m.size());

    iovec local{ .iov_base = buffer.data(), .iov_len = m.size() };
    iovec remote{ .iov_base = reinterpret_cast<void*>(m.start), .iov_len = m.size() };

    const auto rc = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    if (rc < 0)
        return;

    constexpr auto check_and_print_tables_with_offset = [](const auto& buffer, const std::size_t offset) {
        const auto span = std::span<const rc4_entry_raw> {
            reinterpret_cast<const rc4_entry_raw*>(buffer.data() + offset), 
            (buffer.size() - offset) / sizeof(rc4_entry_raw) 
        };

        for(const auto& table : check_map_tables(span))
            print_table(table);
    };

    // for(auto i = 0uz; i < RC4_RAW_TABLE_ELEMENT_SIZE; i++)
    //     check_and_print_tables_with_offset(buffer, i);

    check_and_print_tables_with_offset(buffer, 0);
    check_and_print_tables_with_offset(buffer, 4);
}

int main(int argc, const char** argv)
{
    const auto pid = pid_t{ std::atoi(argv[1]) };

    const auto maps = get_process_maps(pid);
    fprintf(stderr, "number of maps: %lu\n", maps.size());
    for(const auto m : maps)
        check_map(pid, m);
}


