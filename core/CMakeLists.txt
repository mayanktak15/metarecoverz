cmake_minimum_required(VERSION 3.16)
project(metarecoverx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(xfs_scan src/xfs_scan.cpp)
add_executable(btrfs_scan src/btrfs_scan.cpp)
add_executable(reconstruct_csv src/reconstruct_csv.cpp)
add_executable(recover_csv src/recover_csv.cpp)

# If libxfs is available later, you can link it like this:
# find_library(LIBXFS xfs)
# if(LIBXFS)
#   target_link_libraries(xfs_scan PRIVATE ${LIBXFS})
# endif()
