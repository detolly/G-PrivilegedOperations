#!/bin/sh
clang++ -O3 getmem.cpp -o bin/getmem -std=c++23 -Werror -Wall -Wpedantic -Wconversion &
clang++ -O3 getpid.cpp -o bin/getpid -std=c++23 -Werror -Wall -Wpedantic -Wconversion &
clang++ -O3 modhost.cpp -o bin/modhost -std=c++23 -Werror -Wall -Wpedantic -Wconversion &

wait

cp bin/* ../G-Earth/Build/Linux
