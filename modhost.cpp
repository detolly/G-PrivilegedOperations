
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
            if (!current_line.contains("#automated"))
                new_lines.push_back(current_line);
        }
    }
    {
        auto host_file = std::ofstream(host_file_path, std::ios_base::trunc);
        for(auto it = new_lines.begin(); it != new_lines.end(); it++) {
            host_file << *it;
            if (it != new_lines.end() - 1)
                host_file << std::endl;
        }
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


