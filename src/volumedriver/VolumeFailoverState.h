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

#ifndef VOLUME_FAILOVER_STATE_H_
#define VOLUME_FAILOVER_STATE_H_

#include <iosfwd>
#include <string>

namespace volumedriver
{

enum class VolumeFailoverState
{
    OK_SYNC,
    OK_STANDALONE,
    KETCHUP,
    DEGRADED,
};

const std::string&
volumeFailoverStateToString(VolumeFailoverState st);

std::ostream&
operator<<(std::ostream& os,
           const VolumeFailoverState st);

std::istream&
operator>>(std::istream& is,
           VolumeFailoverState& st);

}

#endif // !VOLUME_FAILOVER_STATE_H_