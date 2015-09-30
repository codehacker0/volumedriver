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

#include <youtils/Main.h>
namespace
{
namespace po = boost::program_options;

struct FawltyFSTest : public youtils::TestMainHelper
{
    FawltyFSTest(int argc,
                char** argv)
        : TestMainHelper(argc,
                         argv)
    {}

    virtual void
    setup_logging()
    {
        MainHelper::setup_logging();
    }

    virtual void
    log_extra_help(std::ostream& ostr)
    {
        log_google_test_help(ostr);
    }

    virtual void
    parse_command_line_arguments()
    {
        init_google_test();
    }
};
}

MAIN(FawltyFSTest)

// Local Variables: **
// compile-command: "scons -D -j 4" **
// mode: c++ **
// End: **