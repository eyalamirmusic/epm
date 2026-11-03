cmake_minimum_required(VERSION 3.25)
project(dep LANGUAGES NONE)

# No CPM.cmake exists anywhere in this tree; lockstep's stub answers.
include(CPM)
CPMAddPackage(NAME adder GITHUB_REPOSITORY cpm-cmake/testpack-adder VERSION 1.0.1)
