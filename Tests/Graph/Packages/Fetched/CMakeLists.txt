# A fake source package the consumer asks for with FetchContent.
cmake_minimum_required(VERSION 3.25)
project(Fetched NONE)

add_library(fetched INTERFACE)
add_library(Fetched::fetched ALIAS fetched)
