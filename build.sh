#! /bin/bash
rm -rf build
mkdir build
cd build

cmake ..
make

./demo
# valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --undef-value-errors=no --log-file=log ./demo