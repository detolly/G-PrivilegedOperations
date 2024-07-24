
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

constexpr static auto RC4_RAW_TABLE_ELEMENT_SIZE = 8;
constexpr static auto RC4_TABLE_SIZE = 256;
constexpr static auto RC4_INVALID_MASK_SHOCKWAVE = 0xFFFFFFFB'FFFFFF00;

constexpr static auto RC4_RAW_TABLE_SIZE = RC4_RAW_TABLE_ELEMENT_SIZE * RC4_TABLE_SIZE;

struct map
{
    std::uint64_t start;
    std::uint64_t end;

    std::uint64_t size() const { return end - start; }
};


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

void print_table(const std::array<std::uint8_t, RC4_TABLE_SIZE>& table)
{
    for(const auto entry : table)
        printf("%02x", entry);

    printf("\n");
}

constexpr std::optional<std::array<std::uint8_t, RC4_TABLE_SIZE>> extract_table(const std::span<const std::uint8_t> table_span)
{
    auto table = std::array<std::uint8_t, RC4_TABLE_SIZE>{ 0 };
    auto values_already_seen = std::array<bool, RC4_TABLE_SIZE>{ false };

    if (table_span.size() != RC4_RAW_TABLE_SIZE)
        return {};

    for(auto i = 0uz; i < table_span.size(); i += RC4_RAW_TABLE_ELEMENT_SIZE)
    {
        const auto extracted_value = table_span[i];
        if (values_already_seen[extracted_value])
            return {};

        table[i / RC4_RAW_TABLE_ELEMENT_SIZE] = extracted_value;
        values_already_seen[extracted_value] = true;
    }

    return table;
}

constexpr std::vector<std::array<std::uint8_t, RC4_TABLE_SIZE>> check_map_tables(const std::uint64_t offset, const std::span<const std::uint8_t> buffer)
{
    std::vector<std::array<std::uint8_t, RC4_TABLE_SIZE>> tables;

    auto valid_entries = 0;
    for(auto i = offset; i < buffer.size(); i += RC4_RAW_TABLE_ELEMENT_SIZE)
    {
        const auto value = *reinterpret_cast<const std::uint64_t*>(buffer.data() + i);
        if ((value & RC4_INVALID_MASK_SHOCKWAVE) != 0) {
            valid_entries = 0;
            continue;
        }

        valid_entries++;
        if (valid_entries == RC4_TABLE_SIZE) {
            const auto maybe_table = extract_table({ buffer.data() + i - RC4_RAW_TABLE_SIZE + RC4_RAW_TABLE_ELEMENT_SIZE, RC4_RAW_TABLE_SIZE });
            if (maybe_table.has_value()) {
                tables.push_back(maybe_table.value());
            }
            valid_entries--;
        }
    }

    return tables;
}

void check_map(const pid_t pid, const map m)
{
    std::uint8_t* const buffer = new std::uint8_t[m.size()]{ 0 };

    iovec local{ .iov_base = buffer, .iov_len = m.size() };
    iovec remote{ .iov_base = reinterpret_cast<void*>(m.start), .iov_len = m.size() };

    auto rc = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    if (rc < 0) {
        // perror("Failed to read memory");
        // fprintf(stderr, "\tat %08x\n", static_cast<std::uint32_t>(m.start));
        delete[] buffer;
        return;
    }

    constexpr auto check_and_print_tables_with_offset = [](const auto* const buffer, const std::uint64_t offset, const map _map) {
        for(const auto& table : check_map_tables(offset, { buffer, _map.size() }))
            print_table(table);
    };

    // for(auto i = 0uz; i < RC4_RAW_TABLE_ELEMENT_SIZE; i++)
    //     check_and_print_tables_with_offset(buffer, i, m);

    check_and_print_tables_with_offset(buffer, 0, m);
    check_and_print_tables_with_offset(buffer, 4, m);

    delete[] buffer;
}

int main(int argc, const char** argv)
{
    const auto pid = pid_t{ std::atoi(argv[1]) };

    const auto maps = get_process_maps(pid);
    fprintf(stderr, "number of maps: %lu\n", maps.size());
    for(const auto m : maps)
        check_map(pid, m);
}


