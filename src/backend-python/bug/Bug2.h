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

#ifndef BUG_TWO_H
#define BUG_TWO_H
#include "Bug1.h"

class Bug2
{
public:
    Bug2() = default;

    std::string
    call(const Bug1&)
    {
        //        print "Bug2::call";
    }

    std::string
    str()
    {
        return "Bug2";
    }

};

#endif // BUG_TWO_H
