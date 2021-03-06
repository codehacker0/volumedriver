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

#include "AlbaConfig.h"
#include "BackendConfig.h"
#include "BackendParameters.h"
#include "LocalConfig.h"
#include "MultiConfig.h"
#include "S3Config.h"

#include <sstream>

#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <youtils/Assert.h>
#include <youtils/ConfigFetcher.h>
#include <youtils/JsonString.h>
#include <youtils/Uri.h>

namespace backend
{
namespace fs = boost::filesystem;
namespace ip = initialized_params;
namespace yt = youtils;

std::unique_ptr<BackendConfig>
BackendConfig::makeBackendConfig(const yt::JsonString& json_string)
{
    boost::property_tree::ptree pt;
    std::stringstream ss(json_string.str());

    boost::property_tree::json_parser::read_json(ss,
                                                 pt);
    return makeBackendConfig(pt);
}

std::unique_ptr<BackendConfig>
BackendConfig::makeBackendConfig(const yt::Uri& loc)
{
    std::unique_ptr<yt::ConfigFetcher> fetcher(yt::ConfigFetcher::create(loc));
    return makeBackendConfig((*fetcher)(VerifyConfig::F));
}

std::unique_ptr<BackendConfig>
BackendConfig::makeBackendConfig(const boost::property_tree::ptree& pt)
{
    ip::PARAMETER_TYPE(backend_type) bt(pt);

    switch(bt.value())
    {
    case BackendType::LOCAL :
        return std::unique_ptr<BackendConfig>(new LocalConfig(pt));
    case BackendType::S3:
        return std::unique_ptr<BackendConfig>(new S3Config(pt));
    case BackendType::MULTI:
        return std::unique_ptr<BackendConfig>(new MultiConfig(pt));
    case BackendType::ALBA:
        return std::unique_ptr<BackendConfig>(new AlbaConfig(pt));
    }
    UNREACHABLE
}

const char*
BackendConfig::componentName() const
{
    return ip::backend_connection_manager_name;
}

std::string
BackendConfig::sectionName(const std::string& section)
{
    return std::string(ip::backend_connection_manager_name) + "." + section;
}

}

// Local Variables: **
// mode: c++ **
// End: **
