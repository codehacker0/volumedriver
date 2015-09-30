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

#include "BackwardTLogReader.h"
#include "DataStoreNG.h"
#include "BackendTasks.h"
#include "Entry.h"
#include "MetaDataStoreInterface.h"
#include "SnapshotManagement.h"
#include "TracePoints_tp.h"
#include "TLogReader.h"
#include "TLogReaderUtils.h"
#include "TLogWriter.h"
#include "VolManager.h"
#include "WriteOnlyVolume.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include <boost/scope_exit.hpp>

#include <youtils/Assert.h>
#include <youtils/IOException.h>
#include <youtils/ScopeExit.h>
#include <youtils/UUID.h>
#include <youtils/Weed.h>

#define GETVOLUME() getVolume()
#include "youtils/Catchers.h"

namespace volumedriver
{

namespace yt = youtils;

#define LOCKSNAP \
    boost::lock_guard<lock_type> gs__(snapshot_lock_)

#define ASSERT_SNAP_LOCKED                                      \
    ASSERT_LOCKABLE_LOCKED(snapshot_lock_);

#define LOCKTLOG \
    boost::lock_guard<lock_type> gt__(tlog_lock_)

#define ASSERT_TLOG_LOCKED                      \
    ASSERT_LOCKABLE_LOCKED(tlog_lock_)

#define REQUIRE_CURRENT_TLOG                                            \
    if(not currentTLog_)                                                \
    {                                                                   \
        LOG_ERROR("No currentTLog_, volume " <<                         \
                  static_cast<const std::string&>(VOLNAME()) <<         \
                  " in ZOMBIE state");                                  \
        throw fungi::IOException("No current TLOG, volume in ZOMBIE state"); \
    }

#define VOLNAME()                                                       \
    getVolume()->getName()

SnapshotManagement::SnapshotManagement(const VolumeConfig& cfg,
                                       const RestartContext context)
    : VolumeBackPointer(getLogger__())
    , maxTLogEntries_(0)  //temporarily switching to large tlogs, should be fixed by STDEVLEU-2143
    , snapshot_lock_()
    , tlog_lock_()
    , snapshotPath_(VolManager::get()->getMetaDataPath(cfg))
    , tlogPath_(VolManager::get()->getTLogPath(cfg))
    , nspace_(cfg.getNS())
{
    if(fs::exists(snapshots_file_path_()))
    {
        // its a restart
        VERIFY(context == RestartContext::LocalRestart or
               context == RestartContext::BackendRestart);

        sp.reset(new SnapshotPersistor(snapshots_file_path_()));
        currentTLogName_ = sp->getCurrentTLog();
        const fs::path last_tlog(makeTLogPath(currentTLogName_));

        if(not fs::exists(last_tlog))
        {
            LOG_INFO(cfg.id_ << ": last TLog " << last_tlog <<
                     " not present - assuming it's a BackendRestart");

            THROW_UNLESS(context == RestartContext::BackendRestart);
        }
    }
    else
    {
        // it's a new one
        VERIFY(context == RestartContext::Cloning or
               context == RestartContext::Creation);

        sp.reset(new SnapshotPersistor(cfg.parent()));
        fs::create_directories(snapshotPath_);
        fs::create_directories(tlogPath_);

        LOCKSNAP;
        sp->saveToFile(snapshots_file_path_(), SyncAndRename::T);
        currentTLogName_ = sp->getCurrentTLog();
    }
}

void
SnapshotManagement::initialize(VolumeInterface* vol)
{
    LOG_DEBUG("Initializing SnapshotManagement for Volume " << vol);

    setVolume(vol);

    VolumeInterface *vi = getVolume();
    set_max_tlog_entries(vi->getTLogMultiplier(), vi->getSCOMultiplier());
    VERIFY(maxTLogEntries_ > 0);

    openTLog_();
    sync(boost::none);
}

void
SnapshotManagement::set_max_tlog_entries(const boost::optional<TLogMultiplier>& tm,
                                         SCOMultiplier sm)
{
    maxTLogEntries_ = (tm ? *tm : VolManager::get()->number_of_scos_in_tlog.value()) * sm;
}

bool
SnapshotManagement::exists(const fs::path &dir)
{
     fs::path name = dir / snapshotFilename();
     return fs::exists(name);
}

SnapshotManagement::~SnapshotManagement()
{
    try
    {
        maybeCloseTLog_();
    }
    CATCH_STD_LOGLEVEL_IGNORE(VOLNAME() << ": failed to close TLog", FATAL)
}

bool
SnapshotManagement::currentTLogHasData()
{
    // this sync is necessary to see the last data in the TLOG.
    sync(boost::none);

    TLogReader r(tlogPath_ / currentTLogName_);

    return r.nextLocation() != 0;
}

fs::path
SnapshotManagement::getTLogsPath() const
{
    return tlogPath_;
}

fs::path
SnapshotManagement::makeTLogPath(const std::string&tlogName) const
{
    return tlogPath_ / tlogName;
}

void
SnapshotManagement::tlogPathPrepender(OrderedTLogNames& vec) const
{
    for(unsigned i = 0; i < vec.size(); ++i)
    {
        vec[i] = makeTLogPath(vec[i]).string();
    }
}

fs::path
SnapshotManagement::snapshots_file_path(const fs::path& snaps_path)
{
    return snaps_path / snapshotFilename();
}

fs::path
SnapshotManagement::snapshots_file_path_() const
{
    return snapshots_file_path(snapshotPath_);
}

bool
SnapshotManagement::lastSnapshotOnBackend() const
{
    std::vector<SnapshotNum> snaps;
    {
        LOCKSNAP;
        sp->getAllSnapshots(snaps);
    }
    return snaps.empty() or
        isSnapshotInBackend(snaps.back());
}

void
SnapshotManagement::setAsTemplate(const MaybeCheckSum& maybe_sco_crc)
{
    if(getCurrentBackendSize() != 0 or
       snapshotsEmpty())
    {
        // Change the name here
        UUID uuid;
        createSnapshot("VolumeTemplateSnapshot_" + uuid.str(),
                       maybe_sco_crc,
                       SnapshotMetaData(),
                       uuid);
    }
    {
        LOCKSNAP;
        sp->deleteAllButLastSnapshot();
        sp->saveToFile(snapshots_file_path_(), SyncAndRename::T);
    }
    scheduleWriteSnapshotToBackend();
}

bool
SnapshotManagement::tlogReferenced(const std::string& tlog_name) const
{
    return sp->tlogReferenced(tlog_name);
}

void
SnapshotManagement::createSnapshot(const std::string& name,
                                   const MaybeCheckSum& maybe_sco_crc,
                                   const SnapshotMetaData& metadata,
                                   const UUID& guid,
                                   const bool create_scrubbed)
{
    std::string tlogname;
    SCO sconame;
    CheckSum tlog_crc;

    try
    {
        {
            LOCKSNAP;
            tlogname = sp->getCurrentTLog();

            sp->snapshot(name,
                         metadata,
                         guid,
                         create_scrubbed);

            {
                LOCKTLOG;
                sconame = currentTLog_->getClusterLocation().sco();
                if(maybe_sco_crc)
                {
                    currentTLog_->add(*maybe_sco_crc);
                }

                tlog_crc = closeTLog_();
                currentTLogName_ = sp->getCurrentTLog();
                openTLog_();
            }
            sp->saveToFile(snapshots_file_path_(), SyncAndRename::T);
            // num = sp->getSnapshotNum(name);
            sp->getSnapshotNum(name);
        }

        const fs::path tlogpath(makeTLogPath(tlogname));
        scheduleWriteTLogToBackend(tlogname,
                               tlogpath,
                               sconame,
                               tlog_crc);
    }
    catch (SnapshotPersistor::SnapshotNameAlreadyExists&)
    {
        LOG_ERROR(VOLNAME() << ": could not create snapshot " << name <<
                  ": a snapshot with this name already exists");
        throw;
    }
    CATCH_STD_ALL_VLOG_HALT_RETHROW("could not create snapshot")
}

bool
SnapshotManagement::checkSnapshotUUID(const std::string& snapshotName,
                                      const volumedriver::UUID& uuid) const
{
    LOCKSNAP;
    return sp->checkSnapshotUUID(snapshotName,
                                 uuid);
}

bool
SnapshotManagement::isSnapshotInBackend(const SnapshotNum num) const
{
    LOCKSNAP;
    return sp->isSnapshotInBackend(num);
}

template<typename T>
void
SnapshotManagement::halt_on_error_(T&& op, const char* desc)
{
    try
    {
        op();
        LOG_VTRACE(desc  << ": done");
    }
    CATCH_STD_ALL_VLOG_HALT_RETHROW("could not " << desc);
}

void
SnapshotManagement::scheduleBackendSync(const MaybeCheckSum& maybe_sco_crc)
{
    LOG_VTRACE("cs " << maybe_sco_crc);
    halt_on_error_([&]()
                   {
                       LOCKSNAP;

                       SCO sconame;

                       const std::string tlogname(sp->getCurrentTLog());
                       const fs::path tlogpath(makeTLogPath(tlogname));
                       sp->newTLog();

                       LOCKTLOG;
                       if(maybe_sco_crc)
                       {
                           currentTLog_->add(*maybe_sco_crc);
                       }

                       sconame = currentTLog_->getClusterLocation().sco();
                       const auto tlog_crc(closeTLog_(&tlogpath));

                       DEBUG_CHECK(FileUtils::calculate_checksum(tlogpath) == tlog_crc);

                       currentTLogName_ = sp->getCurrentTLog();
                       openTLog_();

                       tracepoint(openvstorage_volumedriver,
                                  new_tlog,
                                  nspace_.str().c_str(),
                                  currentTLogName_.c_str());

                       sp->saveToFile(snapshots_file_path_(),
                                      SyncAndRename::T);

                       scheduleWriteTLogToBackend(tlogname,
                                              tlogpath,
                                              sconame,
                                              tlog_crc);
                   },
                   "schedule backend sync");
}

void
SnapshotManagement::getCurrentTLogs(OrderedTLogNames& vec,
                                    const AbsolutePath absolute_path) const
{
    {
        LOCKSNAP;
        sp->getCurrentTLogs(vec);
    }

    if(T(absolute_path))
    {
        tlogPathPrepender(vec);
    }
}

const std::string&
SnapshotManagement::getCurrentTLogName() const
{
    LOCKSNAP;
    return currentTLogName_;
}

fs::path
SnapshotManagement::getCurrentTLogPath() const
{
    LOCKSNAP;
    return makeTLogPath(currentTLogName_);
}

SnapshotNum
SnapshotManagement::getSnapshotNumberByName(const std::string& name) const
{
    LOCKSNAP;
    return sp->getSnapshotNum(name);
}

void
SnapshotManagement::eraseSnapshotsAndTLogsAfterSnapshot(SnapshotNum num)
{
    OrderedTLogNames vec;
    {
        LOCKSNAP;

        sp->getTLogsAfterSnapshot(num, vec);

        sp->deleteTLogsAndSnapshotsAfterSnapshot(num);

        {
            LOCKTLOG;
            maybeCloseTLog_();
            currentTLogName_ = sp->getCurrentTLog();
            openTLog_();
        }

        sp->saveToFile(snapshots_file_path_(), SyncAndRename::T);
    }

    // Z42: Huh?
    {
        LOCKTLOG;
    }

    tlogPathPrepender(vec);
    for(size_t i = 0; i < vec.size();++i)
    {
        // ignore delete as some of them are
        // already written to the backend!
        // TODO: it would be more appropriate to just only unlink the
        // CORRECT tlog files
        fs::remove(vec[i]);
    }
    scheduleWriteSnapshotToBackend();
}

void
SnapshotManagement::deleteSnapshot(const std::string& name)
{
    {
        LOCKSNAP;
        sp->deleteSnapshot(sp->getSnapshotNum(name));
        sp->saveToFile(snapshots_file_path_(), SyncAndRename::T);
    }
    scheduleWriteSnapshotToBackend();
}

void
SnapshotManagement::getAllTLogs(OrderedTLogNames& out,
                                AbsolutePath absolute) const
{
    {
        LOCKSNAP;
        sp->getAllTLogs(out,
                        WithCurrent::T);

    }

    if(absolute == AbsolutePath::T)
    {
        tlogPathPrepender(out);
    }
}

bool
SnapshotManagement::getTLogsInSnapshot(SnapshotNum num,
                                       OrderedTLogNames& out,
                                       AbsolutePath absolute) const
{
    bool res = false;
    {
        LOCKSNAP;
        res = sp->getTLogsInSnapshot(num,
                                     out);

    }

    if(T(absolute))
    {
        tlogPathPrepender(out);
    }
    return res;
}

bool
SnapshotManagement::getTLogsInSnapshot(const std::string& snapname,
                                       OrderedTLogNames& out,
                                       AbsolutePath absolute) const
{
    return getTLogsInSnapshot(getSnapshotNumberByName(snapname),
                              out,
                              absolute);
}

void
SnapshotManagement::destroy(const DeleteLocalData delete_local_data)
{
    {
        LOCKTLOG;
        maybeCloseTLog_();
    }

    if(T(delete_local_data))
    {
        LOCKSNAP;
        OrderedTLogNames vec;
        sp->getAllTLogs(vec,
                        WithCurrent::T);

        tlogPathPrepender(vec);

        for(const auto& tlogpath : vec)
        {
            fs::remove(tlogpath);
        }

        fs::remove_all(getTLogsPath());
        fs::remove_all(snapshots_file_path_());
    }
}

void
SnapshotManagement::listSnapshots(std::list<std::string>& snapshots) const
{
    LOCKSNAP;
    std::vector<SnapshotNum> snapshotNums;

    sp->getAllSnapshots(snapshotNums);
    std::sort(snapshotNums.begin(),snapshotNums.end());

    for(size_t i = 0; i< snapshotNums.size(); ++i)
    {
        snapshots.push_back(sp->getSnapshotName(snapshotNums[i]));
    }
}

Snapshot
SnapshotManagement::getSnapshot(const std::string& snapname) const
{
    LOCKSNAP;
    return sp->getSnapshot(snapname);
}

void
SnapshotManagement::openTLog_()
{
    getVolume()->cork(TLog::getTLogIDFromName(currentTLogName_));

    numTLogEntries_ = 0;
    previous_tlog_time_ = time(0);
    if (previous_tlog_time_ == -1)
    {
        LOG_ERROR("Could not get the starting time of this tlog.");
    }

    VERIFY(currentTLog_ == nullptr);

    try
    {
        currentTLog_.reset(new TLogWriter(makeTLogPath(currentTLogName_),
                                          nullptr));
    }
    CATCH_STD_ALL_LOG_RETHROW("could not open new TLOG, entering ZOMBIE volume state")
}

void
SnapshotManagement::syncTLog_(const MaybeCheckSum& maybe_sco_crc)
{
    halt_on_error_([&]()
                   {
                       if (maybe_sco_crc)
                       {
                           currentTLog_->add(*maybe_sco_crc);
                       }
                       currentTLog_->sync();
                   },
                   "sync TLog");
}

CheckSum
SnapshotManagement::closeTLog_(const fs::path* path)
{
    VERIFY(currentTLog_);
    const auto crc(currentTLog_->close());
    currentTLog_.reset();

    if (path)
    {
        DEBUG_CHECK(FileUtils::calculate_checksum(*path) == crc);
    }

    return crc;
}

void
SnapshotManagement::maybeCloseTLog_()
{
    if (currentTLog_)
    {
        closeTLog_();
    }
}

void
SnapshotManagement::scheduleWriteTLogToBackend(const std::string& tlogname,
                                           const fs::path& tlogpath,
                                           const SCO sconame,
                                           const CheckSum& tlog_crc)
{
    try
    {
        std::unique_ptr<backend_task::WriteTLog>
            task(new backend_task::WriteTLog(getVolume(),
                                        tlogpath,
                                        tlogname,
                                        TLog::getTLogIDFromName(tlogname),
                                        sconame,
                                        tlog_crc));

        VolManager::get()->backend_thread_pool()->addTask(task.release());
    }
    CATCH_STD_ALL_VLOG_ADDERROR_HALT_RETHROW("couldn't schedule writing of tlog " <<
                                             (tlogpath / tlogname).string(),
                                             events::VolumeDriverErrorCode::WriteTLog)
}

std::string
SnapshotManagement::getLastSnapshotName() const
{
    std::vector<SnapshotNum> snapshotNums;
    {
        LOCKSNAP;
        sp->getAllSnapshots(snapshotNums);
    }
    if(not snapshotNums.empty())
    {

        SnapshotNum max = *std::max_element(snapshotNums.begin(), snapshotNums.end());
        std::string name;
        {
            LOCKSNAP;
            name = sp->getSnapshotName(max);
        }
        return name;
    }
    return "";
}

fs::path
SnapshotManagement::saveSnapshotToTempFile()
{
    LOCKSNAP;
    fs::path tmp_file = FileUtils::create_temp_file(tlogPath_,
                                           "snapshots");
    sp->saveToFile(tmp_file, SyncAndRename::F);
    return tmp_file;
}

void
SnapshotManagement::scheduleWriteSnapshotToBackend()
{
    try
    {
        backend_task::WriteSnapshot* task = new backend_task::WriteSnapshot(getVolume());
        VolManager::get()->backend_thread_pool()->addTask(task);
    }
    CATCH_STD_ALL_VLOG_ADDERROR_HALT("could not schedule snapshot writing, volume will be halted",
                                     events::VolumeDriverErrorCode::PutSnapshotsToBackend)
}

void
SnapshotManagement::addSCOCRC(const CheckSum& cs)
{
    LOCKSNAP;
    LOCKTLOG;
    REQUIRE_CURRENT_TLOG;

    halt_on_error_([&]()
                   {
                       LOG_VDEBUG("adding SCO CRC for SCO " <<
                                 currentTLog_->getClusterLocation().sco());
                       currentTLog_->add(cs);
                       maybe_switch_tlog_();
                   },
                   "add SCO CRC");
}

void
SnapshotManagement::maybe_switch_tlog_()
{
    ASSERT_LOCKABLE_LOCKED(snapshot_lock_);
    ASSERT_LOCKABLE_LOCKED(tlog_lock_);

    if (numTLogEntries_ >= maxTLogEntries_)
    {
        const std::string tlogname(sp->getCurrentTLog());
        const ClusterLocation location(currentTLog_->getClusterLocation());

        const fs::path tlogpath(makeTLogPath(tlogname));
        sp->newTLog();
        const auto tlog_crc(closeTLog_());
        currentTLogName_ = sp->getCurrentTLog();

        tracepoint(openvstorage_volumedriver,
                   new_tlog,
                   nspace_.str().c_str(),
                   currentTLogName_.c_str());

        // the datastore also logs SCO rollover with INFO, after all
        LOG_VINFO("switching to TLog " << currentTLogName_);

        openTLog_();
        scheduleWriteTLogToBackend(tlogname,
                               tlogpath,
                               location.sco(),
                               tlog_crc);
        sp->saveToFile(snapshots_file_path_(), SyncAndRename::T);
    }
}

void
SnapshotManagement::addClusterEntry(const ClusterAddress address,
                                    const ClusterLocationAndHash& location_and_hash)
{
    LOCKSNAP;
    LOCKTLOG;
    REQUIRE_CURRENT_TLOG;

    halt_on_error_([&]()
                   {
                       currentTLog_->add(address, location_and_hash);
                       ++numTLogEntries_;
                       sp->addCurrentBackendSize(getVolume()->getClusterSize());
                   },
                   "add cluster entry");
}

void
SnapshotManagement::sync(const MaybeCheckSum& maybe_sco_crc)
{
    LOCKTLOG;
    REQUIRE_CURRENT_TLOG;
    syncTLog_(maybe_sco_crc);
}

void
SnapshotManagement::tlogWrittenToBackendCallback(const TLogID& tlogid,
                                                 const SCO sconame)
{
    const TLogName tlog_name(TLog::getName(tlogid));
    std::unique_ptr<SnapshotPersistor> tmp;

    tracepoint(openvstorage_volumedriver,
               tlog_written_callback_start,
               nspace_.str().c_str(),
               tlog_name.c_str(),
               sconame.number());

    auto on_exit(yt::make_scope_exit([&]
                                     {
                                         tracepoint(openvstorage_volumedriver,
                                                    tlog_written_callback_end,
                                                    nspace_.str().c_str(),
                                                    tlog_name.c_str(),
                                                    sconame.number(),
                                                    std::uncaught_exception());
                                     }));

    {
        LOCKSNAP;
        tmp = std::make_unique<SnapshotPersistor>(*sp);
    }

    try
    {
        tmp->setTLogWrittenToBackend(tlogid);

        const fs::path tmp_file(FileUtils::create_temp_file(tlogPath_,
                                                            "snapshots"));
        ALWAYS_CLEANUP_FILE(tmp_file);

        tmp->saveToFile(tmp_file,
                        SyncAndRename::F);
        getVolume()->getBackendInterface()->write(tmp_file,
                                                  snapshotFilename(),
                                                  OverwriteObject::T);
    }
    CATCH_STD_ALL_VLOG_ADDERROR_RETHROW("snapshots file could not be written to backend",
                                        events::VolumeDriverErrorCode::PutSnapshotsToBackend);

    try
    {
        {
            LOCKSNAP;
            sp->setTLogWrittenToBackend(tlogid);
            tmp = std::make_unique<SnapshotPersistor>(*sp);
        }

        tmp->saveToFile(snapshots_file_path_(),
                        SyncAndRename::T);
    }
    CATCH_STD_ALL_VLOG_HALT_RETHROW("problem setting TLog " << tlogid <<
                                    " written to backend");

    try
    {
        if(sconame.asBool())
        {
            // First notify the DataStore and only then set the tlog written to
            // backend?
            getVolume()->getDataStore()->writtenToBackendUpTo(sconame);
        }
    }
    CATCH_STD_ALL_VLOG_HALT_RETHROW("problem setting SCOs up to " << sconame <<
                                    " written to backend")

    try
    {
        getVolume()->unCorkAndTrySync(tlogid);
    }
    CATCH_STD_ALL_VLOG_HALT_RETHROW("problem unCorking tlog " << tlogid)

    try
    {
        const std::string tlogname(TLog::getName(tlogid));

        // This seems to give problems with snapshotrestore.
        // but that should be solved now because tlog are automatically
        // fetched again from the backend as needed --
        fs::remove(getTLogsPath() / tlogname);
        //        checkSumStore_.erase(tlogname);
        if(sconame.asBool())
        {
            getVolume()->removeUpToFromFailOverCache(sconame);
        }
        // This has to happen *after* setting the tlog written to backend as it might
        // otherwise race with the setFailOver on Volume.
        getVolume()->checkState(tlogname);
    }
    CATCH_STD_ALL_VLOGLEVEL_IGNORE("problem after setting TLog written to backend",
                                   WARN)
}

void
SnapshotManagement::getTLogsTillSnapshot(SnapshotNum num,
                                         OrderedTLogNames& out,
                                         AbsolutePath absolute) const
{
    LOCKSNAP;
    sp->getTLogsTillSnapshot(num,
                             out);
    if(T(absolute))
    {
        tlogPathPrepender(out);
    }
}

void
SnapshotManagement::getTLogsAfterSnapshot(SnapshotNum num,
                                   OrderedTLogNames& out,
                                   AbsolutePath absolute) const
{
    LOCKSNAP;
    sp->getTLogsAfterSnapshot(num,
                              out);
    if(T(absolute))
    {
        tlogPathPrepender(out);
    }
}

bool
SnapshotManagement::snapshotExists(SnapshotNum num) const
{
    LOCKSNAP;
    return sp->snapshotExists(num);
}

bool
SnapshotManagement::snapshotExists(const std::string& name) const
{
    LOCKSNAP;
    return sp->snapshotExists(name);
}

const youtils::UUID&
SnapshotManagement::getSnapshotCork(const std::string& snapshot_name) const
{
    LOCKSNAP;
    return sp->getSnapshotCork(snapshot_name);
}

boost::optional<youtils::UUID>
SnapshotManagement::lastCork() const
{
    LOCKSNAP;
    return sp->lastCork();
}

ScrubId
SnapshotManagement::scrub_id() const
{
    LOCKSNAP;
    return sp->scrub_id();
}

ScrubId
SnapshotManagement::new_scrub_id()
{
    ScrubId scrub_id;

    {
        LOCKSNAP;
        scrub_id = std::move(sp->new_scrub_id());
        sp->saveToFile(snapshots_file_path_(),
                       SyncAndRename::T);
    }

    scheduleWriteSnapshotToBackend();
    return scrub_id;
}

ScrubId
SnapshotManagement::replaceTLogsWithScrubbedOnes(const OrderedTLogNames &in,
                                                 const std::vector<TLog>& out,
                                                 SnapshotNum num)
{
    ScrubId scrub_id;
    {
        LOCKSNAP;

        scrub_id = sp->replace(in,
                               out,
                               num);
        sp->setSnapshotScrubbed(num,
                                true);
        sp->saveToFile(snapshots_file_path_(),
                       SyncAndRename::T);
    }

    scheduleWriteSnapshotToBackend();

    return scrub_id;
}

void
SnapshotManagement::setSnapshotScrubbed(const std::string& snapname)
{
    LOCKSNAP;
    SnapshotNum num = sp->getSnapshotNum(snapname);

    sp->setSnapshotScrubbed(num, true);
}

void
SnapshotManagement::getSnapshotScrubbingWork(const boost::optional<std::string>& start_snap,
                                             const boost::optional<std::string>& end_snap,
                                             SnapshotWork& out) const
{
    LOCKSNAP;
    sp->getSnapshotScrubbingWork(start_snap,
                                 end_snap,
                                 out);
}

void
SnapshotManagement::scheduleTLogsToBeWrittenToBackend()
{
    OrderedTLogNames names;
    LOCKSNAP;
    sp->getTLogsNotWrittenToBackend(names);

    for (size_t i = 0; i < names.size() - 1; ++i)
    {
        const fs::path tlogpath(makeTLogPath(names[i]));

        const SCO sconame = BackwardTLogReader(tlogpath).nextClusterLocation().sco();
        const auto tlog_crc(FileUtils::calculate_checksum(tlogpath));

        LOG_INFO("Scheduling: " << tlogpath << " last sconame " << sconame);

        scheduleWriteTLogToBackend(names[i],
                               tlogpath,
                               sconame,
                               tlog_crc);
    }
}

void
SnapshotManagement::getTLogsNotWrittenToBackend(OrderedTLogNames& out) const
{
    LOCKSNAP;
    sp->getTLogsNotWrittenToBackend(out);
}

void
SnapshotManagement::getTLogsWrittenToBackend(OrderedTLogNames& out) const
{
    LOCKSNAP;
    sp->getTLogsWrittenToBackend(out);
}

bool
SnapshotManagement::isSyncedToBackend()
{
    OrderedTLogNames out;
    getTLogsNotWrittenToBackend(out);
    VERIFY(out.size() > 0);
    if(out.size() > 1)
    {
        return false;
    }
    LOCKTLOG;
    REQUIRE_CURRENT_TLOG;
    syncTLog_(boost::none);
    TLogReader r(getCurrentTLogPath());

    return r.nextLocation() == 0;
}

uint64_t
SnapshotManagement::getSnapshotBackendSize(const std::string& name) const
{
    return sp->getSnapshotBackendSize(name);
}

uint64_t
SnapshotManagement::getCurrentBackendSize() const
{
    return sp->getCurrentBackendSize();
}

uint64_t
SnapshotManagement::getTotalBackendSize() const
{
    return sp->getTotalBackendSize();
}

uint64_t
SnapshotManagement::getTLogSizes(const OrderedTLogNames& in)
{
    LOCKSNAP;
    {
        LOCKTLOG;
        REQUIRE_CURRENT_TLOG;
        syncTLog_(boost::none);
    }

    uint64_t totalSize = 0;
    for(OrderedTLogNames::const_iterator it = in.begin();
        it != in.end();
        ++it)
    {
        if(sp->isTLogWrittenToBackend(*it))
        {
            uint64_t size = getVolume()->getBackendInterface()->getSize(*it);
            if(size > Entry::getDataSize())
            {
                totalSize += size - Entry::getDataSize();
            }
        }
        else
        {
            uint64_t size = fs::file_size(makeTLogPath(*it));
                if(size > Entry::getDataSize())
           {
                totalSize += size - Entry::getDataSize();
           }
        }
    }
    return totalSize;
}

ClusterLocation
SnapshotManagement::getLastSCOInBackend()
{
    OrderedTLogNames tlogs_on_backend;
    sp->getTLogsWrittenToBackend(tlogs_on_backend);
    return makeCombinedBackwardTLogReader(tlogPath_,
                                          tlogs_on_backend,
                                          getVolume()->getBackendInterface()->clone())->nextClusterLocation();
}

ClusterLocation
SnapshotManagement::getLastSCO()
{
    OrderedTLogNames tlogs;
    sp->getAllTLogs(tlogs, WithCurrent::T);
    return makeCombinedBackwardTLogReader(getTLogsPath(),
                                          tlogs,
                                          getVolume()->getBackendInterface()->clone())->nextClusterLocation();
}

std::unique_ptr<SnapshotPersistor>
SnapshotManagement::createSnapshotPersistor(BackendInterfacePtr bi)
{
    return bi->getIstreamableObject<SnapshotPersistor>(snapshotFilename(),
                                                       InsistOnLatestVersion::T);
}

void
SnapshotManagement::writeSnapshotPersistor(const SnapshotPersistor& sp,
                                                 BackendInterfacePtr bi)
{
    fs::path f = FileUtils::create_temp_file_in_temp_dir("snapshots");
    ALWAYS_CLEANUP_FILE(f);
    sp.saveToFile(f, SyncAndRename::F);
    bi->write(f, snapshotFilename(), OverwriteObject::T);
}

}

// Local Variables: **
// mode: c++ **
// End: **