// Copyright (C) 2016 iNuron NV
//
// This file is part of Open vStorage Open Source Edition (OSE),
// as available from
//
//      http://www.openvstorage.org and
//      http://www.openvstorage.com.
//
// This file is free software; you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License v3 (GNU AGPLv3)
// as published by the Free Software Foundation, in version 3 as it comes in
// the LICENSE.txt file of the Open vStorage OSE distribution.
// Open vStorage is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY of any kind.

#ifndef YT_PYTHON_BUILDINFO_H_
#define YT_PYTHON_BUILDINFO_H_

#include <string>

namespace youtils
{

namespace python
{

struct BuildInfo
{
    static void
    registerize();

    static std::string
    revision();

    static std::string
    branch();

    static std::string
    timestamp();
};

}

}

#endif // !YT_PYTHON_BUILDINFO_H_
