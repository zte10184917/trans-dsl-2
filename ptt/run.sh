

#g++-10 -std=c++20 -O2 -c nanobench.cpp -o nanobench.o -I../include -I../tests/pt
g++-10 -std=c++20 -O2 -c dsl-test.cpp -o dsl-test.o -I../include -I../tests/pt
g++-10 -std=c++20 -O2 dsl-test.o nanobench.o -L../cmake-build-debug -ltrans-dsl
