
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

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


std::vector<map> get_process_maps(long pid)
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

			auto m = map { .start = std::strtoull(start.c_str(), nullptr, 16), .end = std::strtoull(end.c_str(), nullptr, 16) };
			if (m.size() > 4 * 1024 * 1024)
				continue;

			maps.push_back(m);
		}
	}

	return maps;
}

void check_valid(std::uint8_t* table_ptr, std::size_t table_size)
{
	auto table = std::array<std::uint8_t, 256>{ };
	
	for(auto i = 0; i < table_size; i += RC4_TABLE_ALIGNMENT)
	{
		const auto value = *reinterpret_cast<std::uint64_t*>(table_ptr + i);
		const auto is_valid = (value & RC4_INVALID_MASK_SHOCKWAVE) == 0;

		if (!is_valid)
			return;

		table[i / RC4_TABLE_ALIGNMENT] = static_cast<std::uint8_t>(value & 0xFF);
	}

	for(const auto entry : table)
	{
		printf("%02x ", entry);
	}
	puts("");
}

void check_map_offset(std::uint64_t offset, std::uint8_t* buffer, std::uint64_t buffer_size)
{
	auto valid_entries = 0;
	auto value_to_index = std::array<std::uint16_t, RC4_TABLE_SIZE>{ RC4_INVALID_VALUE };
	auto index_to_value = std::array<std::uint16_t, RC4_TABLE_SIZE>{ RC4_INVALID_VALUE };

	for(auto i = 0; i < buffer_size; i+= RC4_TABLE_ALIGNMENT)
	{
		const auto value = buffer[i];
		const std::uint16_t table_index = (i / RC4_TABLE_ALIGNMENT) % RC4_TABLE_SIZE;

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

			check_valid(buffer + table_pos, table_size);
		}
	}
}

void check_map(long pid, map m)
{
	const auto buffer = new std::uint8_t[m.size()];
	
	const auto proc_memory_path = std::format("/proc/{}/mem", pid);
	auto fp = fopen("/proc/", "rb");
	if (!fp) {
	 	perror("Could not read memory");
		delete[] buffer;
		return;
	}

	fseek(fp, m.start, SEEK_SET);
	fread(buffer, 1, m.size(), fp);
	fclose(fp);

	check_map_offset(0, buffer, m.size());
	delete[] buffer;
}

int main(int argc, const char** argv)
{
	const auto pid = std::atol(argv[1]);

	const auto maps = get_process_maps(pid);
	for(const auto m : maps)
	{
		check_map(pid, m);
	}
}


