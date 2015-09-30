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

#include "BuildInfoString.h"
#include "BuildInfo.h"
#include <sstream>
namespace youtils
{

std::string
buildInfoString()
{
    std::stringstream ss;

    ss << "hg branch: " << BuildInfo::branch << std::endl
       << "hg revision: " << BuildInfo::revision << std::endl
       << "buildTime: " << BuildInfo::buildTime << std::endl;

    return ss.str();
}
}

// Local Variables: **
// bvirtual-targets: ("/bin/vd_test", "libdssvoldrv.a") **
// compile-command: "scons -D --kernel_version=system --ignore-buildinfo -j 5" **
// mode: c++ **
// End: **