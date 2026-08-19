# Copyright (C) 2026 Giorgos Gasparis <gasp.giorgos@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.15)
project(XilinxWin11Deployer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /EHsc /utf-8)
    add_link_options(/SUBSYSTEM:CONSOLE)
else()
    add_compile_options(-Wall -Wextra -O2)
    set(CMAKE_EXE_LINKER_FLAGS "-static")
endif()

add_executable(Xilinx_Win11_Deployer
    src/main.cpp
    src/extractor.cpp
    src/patcher.cpp
    src/drivers.cpp
    src/state_manager.cpp
    src/common.hpp
    src/extractor.hpp
    src/patcher.hpp
    src/drivers.hpp
    src/state_manager.hpp
)

target_link_libraries(Xilinx_Win11_Deployer PRIVATE
    ole32
    uuid
    shell32
    advapi32
    user32
)
