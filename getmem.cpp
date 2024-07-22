
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
constexpr auto RC4_INVALID_VALUE = 0xFFFF;
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
			// 	continue;

			maps.push_back(m);
		}
	}

	return maps;
}

void check_valid(const std::span<const std::uint8_t> table_span)
{
	auto table = std::array<std::uint8_t, RC4_TABLE_SIZE>{ };

	for(auto i = 0uz; i < table_span.size(); i += RC4_TABLE_ALIGNMENT)
	{
		const auto value = *reinterpret_cast<const std::uint64_t*>(&table_span[i]);
		const auto is_valid = (value & RC4_INVALID_MASK_SHOCKWAVE) == 0;

		if (!is_valid)
			return;

		table[i / RC4_TABLE_ALIGNMENT] = static_cast<std::uint8_t>(value & 0xFF);
	}

	for(const auto entry : table)
	{
		printf("%02x", entry);
	}
	puts("");
}

void check_map_offset(std::uint64_t offset, const std::span<const std::uint8_t> buffer)
{
	auto valid_entries = 0;
	auto value_to_index = std::array<std::size_t, RC4_TABLE_SIZE>{ RC4_INVALID_VALUE };
	auto index_to_value = std::array<std::size_t, RC4_TABLE_SIZE>{ RC4_INVALID_VALUE };

	for(auto i = offset; i < buffer.size(); i+= RC4_TABLE_ALIGNMENT)
	{
		const auto value = buffer[i];
		const std::size_t table_index = (i / RC4_TABLE_ALIGNMENT) % RC4_TABLE_SIZE;

		const auto old_value = index_to_value[table_index];
		if (old_value != RC4_INVALID_VALUE)
		{
			value_to_index[old_value] = RC4_INVALID_VALUE;
			index_to_value[table_index] = RC4_INVALID_VALUE;
			valid_entries -= 1;
		}

		const auto is_value_unique = value_to_index[value] == RC4_INVALID_VALUE;
		if (is_value_unique)
			valid_entries += 1;
		else
			index_to_value[value_to_index[value]] = RC4_INVALID_VALUE;

		value_to_index[value] = table_index;
		index_to_value[table_index] = value;

		if (valid_entries == RC4_TABLE_SIZE)
		{
			const auto table_pos = i - (RC4_TABLE_SIZE - 1) * RC4_TABLE_ALIGNMENT;
			const auto table_size = RC4_TABLE_SIZE * RC4_TABLE_ALIGNMENT;

			assert(table_pos + table_size < buffer.size());
			check_valid(buffer.subspan(table_pos, table_size));
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
		printf("\tat %08x\n", static_cast<std::uint32_t>(m.start));
		delete[] buffer;
		return;
	}
	
	// for(std::uint64_t i = 0; i < 8; i++) {
	// 	check_map_offset(i, { buffer, m.size() });
	// }
	check_map_offset(0, { buffer, m.size() });
	check_map_offset(4, { buffer, m.size() });

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


