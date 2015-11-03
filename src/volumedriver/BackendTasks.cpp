// Copyright 2015 iNuron NV
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

#include "BackendTasks.h"
#include "CachedSCO.h"
#include "DataStoreCallBack.h"
#include "Volume.h"
#include "VolumeDriverError.h"
#include "SnapshotManagement.h"
#include "TransientException.h"

#include <youtils/Catchers.h>
#include <youtils/Timer.h>

#include <backend/BackendException.h>

namespace volumedriver
{

namespace backend_task
{

namespace bc = boost::chrono;
namespace yt = youtils;

using ::youtils::FileUtils;
using ::youtils::BarrierTask;

WriteSCO::WriteSCO(VolumeInterface *vol,
                   DataStoreCallBack* cb,
                   SCO sco,
                   const CheckSum& cs,
                   const OverwriteObject overwrite)
    : TaskBase(vol,
               BarrierTask::F)
    , sco_(sco)
    , cb_(cb)
    , cs_(cs)
    , overwrite_(overwrite)
{}

const std::string &
WriteSCO::getName() const
{
    static const std::string name("volumedriver::backend_task::WriteSCO");
    return name;
}

const fs::path
WriteSCO::getSource() const
{
    return cb_->getSCO(sco_, 0)->path();
}

void
WriteSCO::run(int threadid)
{
    try
    {
        yt::SteadyTimer t;

        LOG_TRACE("thread " << threadid);
        const fs::path source(getSource());

        volume_->getBackendInterface()->write(source,
                                              sco_.str(),
                                              overwrite_,
                                              &cs_);

        const auto duration_us(bc::duration_cast<bc::microseconds>(t.elapsed()));
        const uint64_t file_size = fs::file_size(source);

        volume_->SCOWrittenToBackendCallback(file_size,
                                             duration_us);
        cb_->writtenToBackend(sco_);
    }
    catch (backend::BackendInputException &e)
    {
        LOG_ERROR("Failed to put " << volume_->getName() << "/" <<
                  sco_ << " to backend: " << e.what());
        VolumeDriverError::report(events::VolumeDriverErrorCode::PutSCOToBackend,
                                  e.what(),
                                  volume_->getName());
        cb_->reportIOError(sco_,
                           e.what());
        throw;
    }
    CATCH_STD_ALL_EWHAT({
            LOG_INFO("Failed to write SCO " << sco_
                     << " for volume " << volume_->getName() << ": " << EWHAT);
            VolumeDriverError::report(events::VolumeDriverErrorCode::PutSCOToBackend,
                                      EWHAT,
                                      volume_->getName());
            throw;
        });
}

WriteTLog::WriteTLog(VolumeInterface* vol,
                     const fs::path& tlogpath,
                     const TLogId& tlogid,
                     const SCO sconame,
                     const CheckSum& checksum)
    : TaskBase(vol, BarrierTask::T)
    , tlogpath_(tlogpath)
    , tlogid_(tlogid)
    , sconame_(sconame)
    , checksum_(checksum)
{
    if(not fs::exists(tlogpath))
    {
        LOG_ERROR("Tlog does not exist: " << tlogpath);
        throw fungi::IOException("Nonexisting tlog scheduled for write");
    }

    DEBUG_CHECK(FileUtils::calculate_checksum(tlogpath) == checksum);

}

const std::string&
WriteTLog::getName() const
{
    static const std::string name("volumedriver::backend_task::WriteTLog");
    return name;
}

void
WriteTLog::run(int /*threadid*/)
{
    LOG_DEBUG(tlogpath_);

    try
    {
        volume_->getBackendInterface()->write(tlogpath_.string(),
                                              boost::lexical_cast<std::string>(tlogid_),
                                              OverwriteObject::T,
                                              &checksum_);
        volume_->tlogWrittenToBackendCallback(tlogid_,
                                              sconame_);
    }
    catch (backend::BackendInputException& e)
    {
        // Catch non recoverable errors and handle here
        if (not fs::exists(tlogpath_))
        {
            LOG_WARN("Couldn't write tlog " << tlogpath_
                     << "because it's gone, probably snapshot restore");
        }
        else
        {
            LOG_ERROR("Couldn't write tlog " << tlogpath_ << ": checksum mismatch?");
            VolumeDriverError::report(events::VolumeDriverErrorCode::PutTLogToBackend,
                                      e.what(),
                                      volume_->getName());
            volume_->halt();
            throw;
        }
    }
    CATCH_STD_ALL_EWHAT({
            VolumeDriverError::report(events::VolumeDriverErrorCode::PutTLogToBackend,
                                      EWHAT,
                                      volume_->getName());
            throw;
        });
}

WriteSnapshot::WriteSnapshot(VolumeInterface* vol)
    : TaskBase(vol,
               BarrierTask::T)
{}

const std::string&
WriteSnapshot::getName() const
{
    static const std::string name("volumedriver::backend_task::WriteSnapshot");
    return name;
}

void
WriteSnapshot::run(int /*threadID*/)
{
    try
    {
        const fs::path tmpfile = volume_->saveSnapshotToTempFile();
        CheckSum chk = FileUtils::calculate_checksum(tmpfile);
        ALWAYS_CLEANUP_FILE(tmpfile);

        volume_->getBackendInterface()->write(tmpfile,
                                              snapshotFilename(),
                                              OverwriteObject::T,
                                              &chk);
    }
    CATCH_STD_ALL_EWHAT({
            LOG_WARN("Couldn't write snapshot to backend, will be retried: " << EWHAT);
            VolumeDriverError::report(events::VolumeDriverErrorCode::PutSnapshotsToBackend,
                                      EWHAT,
                                      volume_->getName());
            throw;
        });
}

DeleteTLog::DeleteTLog(VolumeInterface* vol,
                       const std::string& tlog)
    : TaskBase(vol, BarrierTask::F)
    , tlog_(tlog)
{}

const std::string&
DeleteTLog::getName() const
{
    static const std::string name("volumedriver::backend_task::DeleteTLog");
    return name;
}

void
DeleteTLog::run(int /*threadID*/)
{
    try
    {
        volume_->getBackendInterface()->remove(tlog_,
                                               ObjectMayNotExist::T);
        LOG_INFO("Deleted TLog " << tlog_);
    }
    CATCH_STD_ALL_LOGLEVEL_IGNORE("Exception deleting TLog " << tlog_,
                                  WARN);
}

BlockDeleteTLogs::BlockDeleteTLogs(VolumeInterface* vol,
                                   VectorType& sources)
    : TaskBase(vol,BarrierTask::F)
    , sources_(sources)
{}

const std::string&
BlockDeleteTLogs::getName() const
{
    static const std::string name("volumedriver::backend_task::BlockDeleteTLogs");
    return name;
}

void
BlockDeleteTLogs::run(int /*threadID*/)
{
    LOG_INFO("Starting block delete of TLogs for volume " << volume_->getName());
    for(VectorType::const_iterator it = sources_.begin();
        it != sources_.end();
        ++it)
    {
        try
        {
            volume_->getBackendInterface()->remove(*it,
                                                   ObjectMayNotExist::T);
            LOG_INFO("Deleted TLog " << *it);

        }
        catch(std::exception& e)
        {
            LOG_WARN("Exception deleleting TLog " << *it << ", " << e.what());
        }
        catch(...)
        {
            LOG_WARN("Unknown exception deleleting TLog " << *it);
        }
    }
    LOG_INFO("Stopping block delete of TLogs for volume " << volume_->getName());
}

BlockDeleteSCOS::BlockDeleteSCOS(VolumeInterface* vol,
                                 VectorType& sources,
                                 const BarrierTask barrier)
    : TaskBase(vol,
               barrier)
    , sources_(sources)
{}

const std::string&
BlockDeleteSCOS::getName() const
{
    static const std::string name("volumedriver::backend_task::BlockDeleteSCOS");
    return name;
}

void
BlockDeleteSCOS::run(int /*threadID*/)
{
    LOG_INFO("Starting block delete of SCOS for volume " << volume_->getName());
    for(VectorType::const_iterator it = sources_.begin();
        it != sources_.end();
        ++it)
    {
        try
        {
            volume_->getBackendInterface()->remove(it->str(),
                                                   ObjectMayNotExist::T);
            LOG_INFO("Deleted SCO " << *it);

        }
        catch(std::exception& e)
        {
            LOG_WARN("Exception deleleting SCO " << *it << ", " << e.what());
        }
        catch(...)
        {
            LOG_WARN("Unknown exception deleleting SCO " << *it);
        }
    }
    LOG_INFO("Stopping block delete of SCOS for volume " << volume_->getName());
}

DeleteSCO::DeleteSCO(VolumeInterface* vol,
                     const SCO sco,
                     const BarrierTask barrier)
    : TaskBase(vol,
               barrier)
    , sco_(sco)
{}

const std::string&
DeleteSCO::getName() const
{
    static const std::string name("volumedriver::backend_task::DeleteSCO");
    return name;
}

void
DeleteSCO::run(int /*threadID*/)
{
    try
    {
        volume_->getBackendInterface()->remove(sco_.str(),
                                               ObjectMayNotExist::T);
        LOG_INFO("Deleted SCO " << sco_);

    }
    catch(std::exception& e)
    {
        LOG_WARN("Ignoring exception deleleting SCO " << sco_ << ", " << e.what());
    }
    catch(...)
    {
        LOG_WARN("Ignoring unknown exception deleleting SCO " << sco_);
    }
}

}

}

// Local Variables: **
// mode: c++ **
// End: **
