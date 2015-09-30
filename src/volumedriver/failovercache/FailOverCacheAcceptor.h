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

#ifndef FAILOVERCACHEACCEPTOR_H
#define FAILOVERCACHEACCEPTOR_H

#include "FailOverCacheProtocol.h"
#include "FailOverCacheWriter.h"
#include "VolumeFailOverCacheWriterMap.h"

#include "../FailOverCacheStreamers.h"

#include <mutex>

#include "fungilib/Mutex.h"
#include "fungilib/ProtocolFactory.h"

#include <youtils/FileDescriptor.h>

namespace failovercache
{


class FailOverCacheAcceptor
    : public fungi::ProtocolFactory
{

public:
    explicit FailOverCacheAcceptor(const boost::filesystem::path& root);

    ~FailOverCacheAcceptor();

    virtual fungi::Protocol*
    createProtocol(fungi::Socket *s,
                   fungi::SocketServer& parentServer);

    virtual const char *
    getName() const
    {
        return "FailOverCacheAcceptor";
    }

    void
    remove(FailOverCacheWriter* writer)
    {
        fungi::ScopedLock l(m);
        return volCacheMap_.remove(writer);
    }

    FailOverCacheWriter*
    lookup(const volumedriver::CommandData<volumedriver::Register>& data)
    {
        fungi::ScopedLock l(m);
        return volCacheMap_.lookup(data.ns_,
                                   data.clustersize_);
    }

    void
    removeProtocol(FailOverCacheProtocol* prot)
    {
        fungi::ScopedLock l(m);
        protocols.remove(prot);
    }

    boost::filesystem::path
    path() const
    {
        return root_;
    }

    boost::filesystem::path
    lock_file() const
    {
        return lock_fd_->path();
    }

private:
    VolumeFailOverCacheWriterMap volCacheMap_;
    fungi::Mutex m;

public:
    const boost::filesystem::path root_;

private:
    typedef std::list<FailOverCacheProtocol*>::iterator iter;
    std::list<FailOverCacheProtocol*> protocols;

    std::unique_ptr<youtils::FileDescriptor> lock_fd_;
    std::unique_lock<youtils::FileDescriptor> file_lock_;
};

}
#endif // FAILOVERCACHEACCEPTOR_H

// Local Variables: **
// mode : c++ **
// End: **