
#include <cstdio>

#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <vector>

const auto host_file_path = "/etc/hosts";
// const auto host_file_path = "hosts";

void remove_hostlines()
{
    std::vector<std::string> new_lines;
    {
        auto host_file = std::ifstream(host_file_path);

        std::string current_line;
        while(std::getline(host_file, current_line))
        {
            if (current_line.find("#automated") == current_line.npos)
                new_lines.push_back(current_line);
        }
    }
    {
        auto host_file = std::ofstream(host_file_path, std::ios_base::trunc);
        for(const auto& line : new_lines)
            host_file << line << std::endl;
    }
}

void add_hostlines(std::vector<std::string> lines)
{
    auto host_file = std::ofstream(host_file_path, std::ios_base::app);
    for(const auto& line : lines)
    {
        host_file << std::endl << (line + " #automated");
    }
}

int main(int argc, const char** argv)
{
    char operation = argv[1][0];

    if (operation == 'a')
    {
        std::vector<std::string> lines;

        for(int i = 2; i < argc; i++)
            lines.push_back(std::string(argv[i]));

        add_hostlines(lines);
    }
    else if (operation == 'd')
    {
        remove_hostlines();
    }
    else 
    {
        std::fprintf(stderr, "Unknown operation");
    }
}


