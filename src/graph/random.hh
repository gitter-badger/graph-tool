// graph-tool -- a general graph modification and manipulation thingy
//
// Copyright (C) 2007-2012 Tiago de Paula Peixoto <tiago@skewed.de>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef RANDOM_HH
#define RANDOM_HH

#include "graph.hh"

#if (GCC_VERSION >= 40400)
#   include <tr1/random>
#else
#   include <boost/tr1/random.hpp>
#endif

typedef std::tr1::mt19937 rng_t;

rng_t get_rng(size_t seed);

#endif
