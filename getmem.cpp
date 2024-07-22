
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

#include <ctre.hpp>

using namespace ctre::literals;

constexpr auto RC4_TABLE_ALIGNMENT = 8;
constexpr auto RC4_TABLE_SIZE = 256;
constexpr auto RC4_INVALID_MASK_SHOCKWAVE = 0xFFFFFFFB'FFFFFF00;

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
        if (const auto [matches, start_view, end_view, permissions_view, n1, n2, n3, trash] = 
            ctre::match<"([A-Fa-f0-9]+)-([A-Fa-f0-9]+)\\s([rwxp\\-]{4})\\s([A-Fa-f0-9]+)\\s(\\d+:\\d+)\\s(\\d+)\\s+(.*)">(current_line); matches)
        {
            const auto start = std::string(start_view);
            const auto end = std::string(end_view);

            if (!permissions_view.view().contains("r"))
                continue;

            const auto m = map { .start = std::strtoull(start.c_str(), nullptr, 16), .end = std::strtoull(end.c_str(), nullptr, 16) };
            // if (m.size() > 4 * 1024 * 1024)
            //     continue;

            maps.push_back(m);
        }
    }

    return maps;
}

void extract_table(const std::span<const std::uint8_t> table_span)
{
    auto table = std::array<std::uint8_t, RC4_TABLE_SIZE>{ 0 };
    auto values_already_seen = std::array<bool, RC4_TABLE_SIZE>{ false };

    for(auto i = 0uz; i < table_span.size(); i += RC4_TABLE_ALIGNMENT)
    {
        const auto value = *reinterpret_cast<const std::uint64_t*>(&table_span[i]);
        const auto extracted_value = static_cast<std::uint8_t>(value & 0xFF);

        if (values_already_seen[extracted_value])
            return;

        table[i / RC4_TABLE_ALIGNMENT] = extracted_value;
        values_already_seen[extracted_value] = true;
    }

    for(const auto entry : table)
        printf("%02x", entry);
    
    printf("\n");
}

void check_map_tables(std::uint64_t offset, const std::span<const std::uint8_t> buffer)
{
    auto valid_entries = 0;

    for(auto i = offset; i < buffer.size(); i += RC4_TABLE_ALIGNMENT)
    {
        const auto value = *reinterpret_cast<const std::uint64_t*>(&buffer[i]);
    
        if ((value & RC4_INVALID_MASK_SHOCKWAVE) != 0) {
            valid_entries = 0;
            continue;
        }
        
        valid_entries++;

        if (valid_entries == RC4_TABLE_SIZE) {
            constexpr auto size = (RC4_TABLE_SIZE - 1) * RC4_TABLE_ALIGNMENT;
            extract_table(buffer.subspan(i - size, size));
            valid_entries--;
        }
    }
}

void check_map(pid_t pid, map m)
{
    std::uint8_t* const buffer = new std::uint8_t[m.size()]{ 0 };

    iovec local{ .iov_base = buffer, .iov_len = m.size() };
    iovec remote{ .iov_base = reinterpret_cast<void*>(m.start), .iov_len = m.size() };

    auto rc = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    if (rc < 0) {
        perror("Failed to read memory");
        fprintf(stderr, "\tat %08x\n", static_cast<std::uint32_t>(m.start));
        delete[] buffer;
        return;
    }

    // for(std::uint64_t i = 0; i < 8; i++) {
    //     check_map_offset(i, { buffer, m.size() });
    // }
    check_map_tables(0, { buffer, m.size() });
    check_map_tables(4, { buffer, m.size() });

    delete[] buffer;
}

int main(int argc, const char** argv)
{
    const auto pid = pid_t{ std::atoi(argv[1]) };

    const auto maps = get_process_maps(pid);

    for(const auto m : maps)
    {
        check_map(pid, m);
    }
}


