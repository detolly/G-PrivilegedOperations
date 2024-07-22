#include <cstdio>
#include <unistd.h>

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>

int main(int argc, const char** argv)
{
    std::vector<std::string> terms;

    for(auto i = 1; i < argc; i++)
        terms.push_back(std::string(argv[i]));

    auto pid = getpid();
    const auto pid_string = std::to_string(pid);

    const auto proc_path = std::filesystem::path("/proc/");
    for (const auto& entry : std::filesystem::directory_iterator(proc_path))
    {
        if (!entry.is_directory())
            continue;
        
        const auto entry_path = entry.path();
        const auto entry_filename = entry_path.filename().string();
        
        if (entry_filename == pid_string)
            continue;

        if (entry_filename.find("self") != entry_filename.npos
            || entry_filename.find("thread_self") != entry_filename.npos)
            continue;
        
        const auto command_line_file = entry_path / "cmdline";
        auto file_stream = std::ifstream(command_line_file.string());
        
        auto string_stream = std::stringstream();
        string_stream << file_stream.rdbuf();
        const auto contents = string_stream.str();
        
        for(const auto& term : terms)
            if (contents.find(term) != contents.npos)
                std::puts(entry_filename.c_str());
    }
}


