// Copyright 2015 Open vStorage NV
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef META_DATA_STORE_STATS_H_
#define META_DATA_STORE_STATS_H_

// this file is part of the volumedriver api
#include <stdint.h>

#include <vector>

#include <youtils/UUID.h>

namespace volumedriver
{

struct MetaDataStoreStats
{
    MetaDataStoreStats()
        : cache_hits(0)
        , cache_misses(0)
        , used_clusters(0)
        , max_pages(0)
        , cached_pages(0)
    {}

    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t used_clusters;
    uint32_t max_pages;
    uint32_t cached_pages;
    // contains also discarded clusters!
    std::vector< std::pair<youtils::UUID, uint64_t> > corked_clusters;
};

}

#endif // !META_DATA_STORE_STATS_H_

// namespace volumedriver
// Local Variables: **
// mode: c++ **
// End: **