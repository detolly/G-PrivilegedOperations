#!/bin/sh
clang++ -O3 getmem.cpp -o bin/getmem -std=c++23 &
clang++ -O3 getpid.cpp -o bin/getpid -std=c++23 &
clang++ -O3 modhost.cpp -o bin/modhost -std=c++23 &

wait

cp bin/* ../G-Earth/Build/Linux
