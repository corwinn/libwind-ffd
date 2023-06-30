# /**** BEGIN LICENSE BLOCK ****
#
# BSD 3-Clause License
#
# Copyright (c) 2023, the wind.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# **** END LICENCE BLOCK ****/

MAKEFLAGS += rR
MODEL ?= stl

APP = libwind-ffd.so
CC ?= ccache clang
CXX ?= ccache clang++
_W  = -Wall -Wextra -Wshadow
_O  = -O0 -g -DFFD_DEBUG -fno-exceptions -fno-threadsafe-statics -gdwarf-4
ifeq ($Q, 1)
_F = -fvisibility=hidden
else
_F = -fsanitize=address,undefined,integer,leak -fvisibility=hidden
endif
_I  = -I. -I$(MODEL)
_L  = -L. -lwind-ffd -Wl,-rpath="${PWD}"
CXXFLAGS = $(_I) -std=c++14 -fPIC $(_O) $(_F) $(_W)
SRC = $(wildcard *.cpp)
SRC := $(filter-out test.cpp,$(SRC))
OBJ = $(patsubst %.cpp,%.o,$(SRC))

$(APP): $(OBJ)
	$(CXX) $(_F) $(OBJ) -shared -o $@

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $< -o $@

libwind-ffd.a: $(OBJ)
	ar rcs $@ $(OBJ)

clean:
	find -type f -iname "*~" -delete -o -iname "*.o" -delete
	rm -f libwind-ffd.a $(APP)

test: $(APP)
	$(CXX) $(CXXFLAGS) test.cpp $(_L) -o test -lz

TEST:
	@echo $(SRC)
	@echo $(OBJ)
	@echo $(TARGET)
	@echo $@

cake: universe

starship: together
