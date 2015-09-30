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

#include "Api.h"
#include "MetaDataStoreInterface.h"
#include "SnapshotManagement.h"
#include "TransientException.h"
#include "VolManager.h"

#include <youtils/BuildInfoString.h>
#include <youtils/Logger.h>
#include "failovercache/fungilib/Mutex.h"

using namespace volumedriver;

namespace be = backend;
namespace bpt = boost::property_tree;

// this one also comes in via Types.h - clean this up.
// namespace fs = boost::filesystem;
namespace vd = volumedriver;

namespace
{
DECLARE_LOGGER("Api");
}

fungi::Mutex&
api::getManagementMutex()
{
    return VolManager::get()->mgmtMutex_;
}

void
api::createNewVolume(const vd::VanillaVolumeConfigParameters& params,
                     const CreateNamespace create_namespace)
{
    VolManager::get()->createNewVolume(params,
                                       create_namespace);
}

WriteOnlyVolume*
api::createNewWriteOnlyVolume(const vd::WriteOnlyVolumeConfigParameters& params)
{
    return VolManager::get()->createNewWriteOnlyVolume(params);
}

void
api::createClone(const vd::CloneVolumeConfigParameters& params,
                 const vd::PrefetchVolumeData prefetch,
                 vd::CreateNamespace create_namespace)
{
    VolManager::get()->createClone(params,
                                   prefetch,
                                   create_namespace);
}

void
api::updateMetaDataBackendConfig(vd::Volume* vol,
                                 const vd::MetaDataBackendConfig& mdb)
{
    vol->updateMetaDataBackendConfig(mdb);
}

void
api::updateMetaDataBackendConfig(const vd::VolumeId& volume_id,
                                 const vd::MetaDataBackendConfig& mdb)
{
    auto vol = VolManager::get()->findVolume_(volume_id);
    updateMetaDataBackendConfig(vol,
                                mdb);
}

void
api::Write(Volume* vol,
           const uint64_t lba,
           const uint8_t *buf,
           const uint64_t buflen)
{
    vol->write(lba, buf, buflen);
}

void
api::Read(Volume* vol,
          const uint64_t lba,
          uint8_t *buf,
          const uint64_t buflen)
{
    vol->read(lba,
              buf,
              buflen);
}

void
api::Write(WriteOnlyVolume* vol,
           uint64_t lba,
           const uint8_t *buf,
           uint64_t buflen)
{
    vol->write(lba, buf, buflen);
}

void
api::Sync(Volume* vol)
{
    vol->sync();
}

uint64_t
api::GetSize(Volume* vol)
{
    return vol->getSize();
}

void
api::Resize(Volume* vol, uint64_t clusters)
{
    vol->resize(clusters);
}

uint64_t
api::GetLbaSize(vd::Volume* vol)
{
    return vol->getLBASize();
}

uint64_t
api::GetClusterSize()
{
    return vd::VolumeConfig::default_cluster_size();
}

void
api::Init(const fs::path& path,
          events::PublisherPtr event_publisher)
{
    vd::VolManager::start(path,
                          event_publisher);
}

void
api::Init(const bpt::ptree& pt,
          events::PublisherPtr event_publisher)
{
    vd::VolManager::start(pt,
                          event_publisher);
}

void
api::Exit(void)
{
    vd::VolManager::stop();
}

vd::Volume*
api::getVolumePointer(const VolumeId& volname)
{
    return VolManager::get()->findVolume_(volname);
}

vd::Volume*
api::get_volume_pointer_no_throw(const VolumeId& volname)
{
    return VolManager::get()->findVolume_noThrow_(volname);
}

vd::Volume*
api::getVolumePointer(const Namespace& ns)
{
    return VolManager::get()->findVolume_(ns);
}

const vd::VolumeId
api::getVolumeId(vd::Volume* vol)
{
    return vol->getName();
}

bool
api::volumeExists(const VolumeId& volname)
{
    return VolManager::get()->findVolume_noThrow_(volname) != nullptr;
}

void
api::getVolumeList(std::list<VolumeId>& l)
{
    VolManager::get()->getVolumeList(l);
}

void
api::getVolumesOverview(std::map<vd::VolumeId, vd::VolumeOverview>& out_map)
{
    VolManager::get()->getVolumesOverview(out_map);
}

VolumeConfig
api::getVolumeConfig(const vd::VolumeId& volName)
{
    const Volume* v = VolManager::get()->findVolume_(volName);
    return v->get_config();
}

void
api::local_restart(const Namespace& ns,
                   const vd::OwnerTag owner_tag,
                   const vd::FallBackToBackendRestart fallback,
                   const vd::IgnoreFOCIfUnreachable ignoreFOCIfUnreachable,
                   const uint32_t num_pages_cached)
{
    VolManager::get()->local_restart(ns,
                                     owner_tag,
                                     fallback,
                                     ignoreFOCIfUnreachable,
                                     num_pages_cached);
}

void
api::backend_restart(const Namespace& ns,
                     const vd::OwnerTag owner_tag,
                     const vd::PrefetchVolumeData prefetch,
                     const vd::IgnoreFOCIfUnreachable ignoreFOCIfUnreachable,
                     const uint32_t num_pages_cached)
{
    VolManager::get()->backend_restart(ns,
                                       owner_tag,
                                       prefetch,
                                       ignoreFOCIfUnreachable,
                                       num_pages_cached);
}

WriteOnlyVolume*
api::restartVolumeWriteOnly(const Namespace& ns,
                            const vd::OwnerTag owner_tag)
{
    return VolManager::get()->restartWriteOnlyVolume(ns,
                                                     owner_tag);
}

void
api::destroyVolume(const vd::VolumeId& volName,
                   const vd::DeleteLocalData delete_local_data,
                   const vd::RemoveVolumeCompletely remove_volume_completely,
                   const vd::DeleteVolumeNamespace delete_volume_namespace,
                   const vd::ForceVolumeDeletion force_volume_deletion)
{
    VolManager::get()->destroyVolume(volName,
                                     delete_local_data,
                                     remove_volume_completely,
                                     delete_volume_namespace,
                                     force_volume_deletion);
}

void
api::destroyVolume(vd::Volume* vol,
                   const vd::DeleteLocalData delete_local_data,
                   const vd::RemoveVolumeCompletely remove_volume_completely,
                   const vd::DeleteVolumeNamespace delete_volume_namespace,
                   const vd::ForceVolumeDeletion force_volume_deletion)

{
    VolManager::get()->destroyVolume(vol,
                                     delete_local_data,
                                     remove_volume_completely,
                                     delete_volume_namespace,
                                     force_volume_deletion);
}

void
api::destroyVolume(WriteOnlyVolume* vol,
                   const RemoveVolumeCompletely remove_volume_completely)
{
    VolManager::get()->destroyVolume(vol,
                                     remove_volume_completely);
}

void
api::removeLocalVolumeData(const be::Namespace& nspace)
{
    VolManager::get()->removeLocalVolumeData(nspace);
}

std::string
api::createSnapshot(vd::Volume* v,
                    const vd::SnapshotMetaData& metadata,
                    const std::string* const snapid,
                    const UUID& uuid)
{
    VERIFY(v);

    std::string snapname;

    if (snapid == nullptr or
        snapid->empty())
    {
        // boost certainly has a nice wrapper around that?
        struct timeval tv;
        if (gettimeofday(&tv, 0) == -1)
        {
            throw fungi::IOException(v->getName().c_str(),
                                     "failed to get timestamp for snapshot");
        }

        time_t timeT = tv.tv_sec;
        struct tm* tm = localtime(&timeT);
        char s[1024];

        if (strftime(s, 1024, "%a %b %Y %T ", tm) == 0)
        {
            throw fungi::IOException(v->getName().c_str(),
                                     "failed to get timestamp for snapshot");
        }
        snapname = std::string(s) + UUID().str();
    }
    else
    {
        snapname = *snapid;
    }

    v->createSnapshot(snapname,
                      metadata,
                      uuid);
    return snapname;
}


std::string
api::createSnapshot(vd::WriteOnlyVolume* v,
                    const vd::SnapshotMetaData& metadata,
                    const std::string* const snapid,
                    const UUID& uuid)
{
    VERIFY(v);

    std::string snapname;

    if (snapid == nullptr or
        snapid->empty())
    {
        // boost certainly has a nice wrapper around that?
        struct timeval tv;
        if (gettimeofday(&tv, 0) == -1)
        {
            throw fungi::IOException(v->getName().c_str(),
                                     "failed to get timestamp for snapshot");
        }

        time_t timeT = tv.tv_sec;
        struct tm* tm = localtime(&timeT);
        char s[1024];

        if (strftime(s, 1024, "%a %b %Y %T ", tm) == 0)
        {
            throw fungi::IOException(v->getName().c_str(),
                                     "failed to get timestamp for snapshot");
        }
        snapname = std::string(s) + UUID().str();
    }
    else
    {
        snapname = *snapid;
    }

    v->createSnapshot(snapname,
                      metadata,
                      uuid);
    return snapname;
}

Snapshot
api::getSnapshot(const vd::VolumeId& id,
                 const std::string& snapname)
{
    ASSERT_LOCKABLE_LOCKED(getManagementMutex());
    const auto vol = VolManager::get()->findVolume_(id);
    return getSnapshot(vol, snapname);
}

Snapshot
api::getSnapshot(const vd::Volume* vol,
                 const std::string& snapname)
{
    VERIFY(vol);
    return vol->getSnapshot(snapname);
}

Snapshot
api::getSnapshot(const vd::WriteOnlyVolume* vol,
                 const std::string& snapname)
{
    VERIFY(vol);
    return vol->getSnapshot(snapname);
}

bool
api::checkSnapshotUUID(const WriteOnlyVolume* vol,
                       const std::string& snapshotName,
                       const vd::UUID& uuid)
{
    VERIFY(vol);
    return vol->checkSnapshotUUID(snapshotName,
                                  uuid);
}

bool
api::snapshotExists(const vd::WriteOnlyVolume* vol,
                    const std::string& snapshotName)
{
    VERIFY(vol);
    return vol->snapshotExists(snapshotName);
}

std::string
api::createSnapshot(const VolumeId& volName,
                    const vd::SnapshotMetaData& metadata,
                    const std::string* const snapid)
{
    auto v = VolManager::get()->findVolume_(volName);
    return createSnapshot(v,
                          metadata,
                          snapid);
}

void
api::setAsTemplate(const VolumeId& volId)
{
    VolManager::get()->setAsTemplate(volId);
}

void
api::showSnapshots(const VolumeId& volName,
                   std::list<std::string>& l)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    v->listSnapshots(l);
}

void
api::showSnapshots(const WriteOnlyVolume* v,
                   std::list<std::string>& l)
{
    VERIFY(v);
    v->listSnapshots(l);
}

void
api::destroySnapshot(const VolumeId& volName,
                     const std::string& snapid)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    v->deleteSnapshot(snapid);
}

void
api::restoreSnapshot(const VolumeId& volName,
                     const std::string& snapid)
{
    VolManager::get()->restoreSnapshot(volName, snapid);
}

void
api::restoreSnapshot(WriteOnlyVolume* vol,
                     const std::string& snapid)
{
    VERIFY(vol);
    vol->restoreSnapshot(snapid);
}

// void
// api::getSnapshotScrubbingWork(const vd::VolumeId& volName,
//                               const boost::optional<std::string>& start_snap,
//                               const boost::optional<std::string>& end_snap,
//                               VolumeWork& work)
// {
//     Volume* v = VolManager::get()->findVolume_(volName);
//     v->getSnapshotScrubbingWork(start_snap, end_snap, work);
// }

uint64_t
api::VolumeDataStoreWriteUsed(const vd::VolumeId& volName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    return v->VolumeDataStoreWriteUsed();
}

uint64_t
api::VolumeDataStoreReadUsed(const vd::VolumeId& volName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    return v->VolumeDataStoreReadUsed();
}

PerformanceCounters&
api::performance_counters(const vd::VolumeId& volName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    return v->performance_counters();
}

bool
api::getHalted(const vd::VolumeId& volName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    return v->is_halted();
}

uint64_t
api::getCacheHits(const vd::VolumeId& volName)
{
    Volume* v =  VolManager::get()->findVolume_(volName);
    return v->getCacheHits();
}

uint64_t
api::getCacheMisses(const vd::VolumeId& volName)
{
    Volume* v =  VolManager::get()->findVolume_(volName);
    return v->getCacheMisses();
}

uint64_t
api::getNonSequentialReads(const vd::VolumeId& volName)
{
    Volume* v =  VolManager::get()->findVolume_(volName);
    return v->getNonSequentialReads();
}

uint64_t
api::getClusterCacheHits(const vd::VolumeId& volName)
{
    Volume* v =  VolManager::get()->findVolume_(volName);
    return v->getClusterCacheHits();
}

uint64_t
api::getClusterCacheMisses(const vd::VolumeId& volName)
{
    Volume* v =  VolManager::get()->findVolume_(volName);
    return v->getClusterCacheMisses();
}

uint64_t
api::getQueueCount(const VolumeId& volName)
{
    return VolManager::get()->getQueueCount(volName);
}

uint64_t
api::getQueueSize(const VolumeId& volName)
{
    return VolManager::get()->getQueueSize(volName);
}

uint64_t
api::getTLogUsed(const vd::VolumeId& volName)
{
    Volume* v =  VolManager::get()->findVolume_(volName);
    return v->getTLogUsed();
}

void
api::scheduleBackendSync(const vd::VolumeId& volName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    v->scheduleBackendSync();
}

bool
api::isVolumeSynced(const vd::VolumeId& volName)
{

    Volume* v = VolManager::get()->findVolume_(volName);
    return v->isSyncedToBackend();
}

bool
api::isVolumeSyncedUpTo(const vd::VolumeId& volName,
                        const std::string& snapshotName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    return v->isSyncedToBackendUpTo(snapshotName);
}

bool
api::isVolumeSyncedUpTo(const WriteOnlyVolume* v,
                        const std::string& snapshotName)
{
    VERIFY(v);
    return v->isSyncedToBackendUpTo(snapshotName);
}

std::string
api::getRevision()
{
    return youtils::buildInfoString();

}

void
api::removeNamespaceFromSCOCache(const backend::Namespace& nsName)
{
    VolManager::get()->getSCOCache()->removeDisabledNamespace(nsName);
}

void
api::getSCOCacheMountPointsInfo(vd::SCOCacheMountPointsInfo& info)
{
    return VolManager::get()->getSCOCache()->getMountPointsInfo(info);
}

vd::SCOCacheNamespaceInfo
api::getVolumeSCOCacheInfo(const backend::Namespace ns)
{
    return VolManager::get()->getSCOCache()->getNamespaceInfo(ns);
}

vd::VolumeFailoverState
api::getFailOverMode(const vd::VolumeId& volName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    return v->getVolumeFailoverState();
}

boost::optional<vd::FailOverCacheConfig>
api::getFailOverCacheConfig(const vd::VolumeId& volName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    return v->getFailOverCacheConfig();
}

void
api::setFailOverCacheConfig(const vd::VolumeId& volName,
                            const boost::optional<vd::FailOverCacheConfig>& maybe_foc_config)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    v->setFailOverCacheConfig(maybe_foc_config);
}

uint64_t
api::getCurrentSCOCount(const vd::VolumeId& volName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    return  v->getSnapshotSCOCount();
}

uint64_t
api::getSnapshotSCOCount(const vd::VolumeId& volName,
                         const std::string& snapid)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    return v->getSnapshotSCOCount(snapid);
}

void
api::setFOCTimeout(const vd::VolumeId& volName,
                   uint32_t timeout)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    v->setFOCTimeout(timeout);
}

bool
api::checkVolumeConsistency(const vd::VolumeId& volName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    return v->checkConsistency();
}

uint64_t
api::getSnapshotBackendSize(const vd::VolumeId& volName,
                            const std::string& snapName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    return v->getSnapshotBackendSize(snapName);
}

uint64_t
api::getCurrentBackendSize(const vd::VolumeId& volName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    return v->getCurrentBackendSize();
}


void
api::dumpSCOInfo(const std::string& outFile)
{
    VolManager::get()->getSCOCache()->dumpSCOInfo(outFile);
}


void
api::addSCOCacheMountPoint(const std::string& path,
                           uint64_t size)
{
    MountPointConfig cfg(path, size);
    VolManager::get()->getSCOCache()->addMountPoint(cfg);
}

void
api::removeSCOCacheMountPoint(const std::string& path)
{
    VolManager::get()->getSCOCache()->removeMountPoint(path);
}

void
api::addClusterCacheDevice(const std::string& /*path*/,
                      const uint64_t /*size*/)
{
    //    VolManager::get()->getClusterCache().addDevice(path,
    //    size);
    LOG_WARN( "Not supported in this release");
    throw fungi::IOException("Not Supported in this release");

}

void
api::removeClusterCacheDevice(const std::string& /*path*/)
{
    //    VolManager::get()->getClusterCache().removeDevice(path);;
    LOG_WARN("Not supported in this release");
    throw fungi::IOException("Not Supported in this release");
}

void
api::setClusterCacheDeviceOffline(const std::string& path)
{
    VolManager::get()->getClusterCache().offlineDevice(path);
}

void
api::setClusterCacheDeviceOnline(const std::string& path)
{
    VolManager::get()->getClusterCache().onlineDevice(path);
}

void
api::getClusterCacheDeviceInfo(vd::ClusterCache::ManagerType::Info& info)
{
    VolManager::get()->getClusterCacheDeviceInfo(info);
}

vd::ClusterCacheVolumeInfo
api::getClusterCacheVolumeInfo(const vd::VolumeId& volName)
{
    return VolManager::get()->findVolume_(volName)->getClusterCacheVolumeInfo();
}

// void
// api::changeThrottling(unsigned throttle_usecs)
// {
//     VolManager::get()->setThrottleUsecsdatastore_throttle_usecs.value().store(throttle_usecs);
// }

void
api::startPrefetching(const vd::VolumeId& volName)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    v->startPrefetch();
}

void
api::getClusterCacheStats(uint64_t& num_hits,
                          uint64_t& num_misses,
                          uint64_t& num_entries)
{
    VolManager::get()->getClusterCacheStats(num_hits,
                                            num_misses,
                                            num_entries);
}

vd::MetaDataStoreStats
api::getMetaDataStoreStats(const vd::VolumeId& volname)
{
    MetaDataStoreStats mdss;
    vd::MetaDataStoreInterface* mdi =
        VolManager::get()->findVolume_(volname)->getMetaDataStore();
    mdi->getStats(mdss);
    return mdss;
}

boost::variant<youtils::UpdateReport,
               youtils::ConfigurationReport>
api::updateConfiguration(const bpt::ptree& pt)
{
    return VolManager::get()->updateConfiguration(pt);
}

boost::variant<youtils::UpdateReport,
               youtils::ConfigurationReport>
api::updateConfiguration(const fs::path& path)
{
    return VolManager::get()->updateConfiguration(path);
}

void
api::persistConfiguration(const fs::path& path,
                          const bool reportDefault)
{
    VolManager::get()->persistConfiguration(path,
                                            reportDefault ?
                                            ReportDefault::T :
                                            ReportDefault::F);
}

void
api::persistConfiguration(bpt::ptree& pt,
                          const bool reportDefault)
{
    VolManager::get()->persistConfiguration(pt,
                                            reportDefault ?
                                            ReportDefault::T :
                                            ReportDefault::F);
}

std::string
api::persistConfiguration(const bool reportDefault)
{
    return VolManager::get()->persistConfiguration(reportDefault ?
                                                   ReportDefault::T :
                                                   ReportDefault::F);
}

bool
api::checkConfiguration(const fs::path& path,
                        ConfigurationReport& c_rep)
{
    return VolManager::get()->checkConfiguration(path,
                                                 c_rep);
}

uint64_t
api::getStored(const vd::VolumeId& volName)
{
    return VolManager::get()->findVolume_(volName)->getTotalBackendSize();
}

bool
api::getVolumeDriverReadOnly()
{
    return VolManager::get()->readOnlyMode();
}

void
api::setClusterCacheBehaviour(const vd::VolumeId& volName,
                              const boost::optional<vd::ClusterCacheBehaviour> b)
{
    VolManager::get()->findVolume_(volName)->set_cluster_cache_behaviour(b);
}


boost::optional<vd::ClusterCacheBehaviour>
api::getClusterCacheBehaviour(const vd::VolumeId& volName)
{
    return VolManager::get()->findVolume_(volName)->get_cluster_cache_behaviour();
}

void
api::setClusterCacheMode(const vd::VolumeId& volName,
                         const boost::optional<vd::ClusterCacheMode>& m)
{
    VolManager::get()->findVolume_(volName)->set_cluster_cache_mode(m);
}


boost::optional<vd::ClusterCacheMode>
api::getClusterCacheMode(const vd::VolumeId& volName)
{
    return VolManager::get()->findVolume_(volName)->get_cluster_cache_mode();
}

void
api::setClusterCacheLimit(const vd::VolumeId& volName,
                          const boost::optional<vd::ClusterCount>& l)
{
    VolManager::get()->findVolume_(volName)->set_cluster_cache_limit(l);
}

boost::optional<vd::ClusterCount>
api::getClusterCacheLimit(const vd::VolumeId& volName)
{
    return VolManager::get()->findVolume_(volName)->get_cluster_cache_limit();
}

void
api::getScrubbingWork(const vd::VolumeId& volName,
                      std::vector<std::string>& scrubbing_work_units,
                      const boost::optional<std::string>& start_snap,
                      const boost::optional<std::string>& end_snap)
{
    return VolManager::get()->findVolume_(volName)->getScrubbingWork(scrubbing_work_units,
                                                                     start_snap,
                                                                     end_snap);
}


void
api::applyScrubbingWork(const vd::VolumeId& volName,
                        const std::string& scrub_work,
                        const vd::CleanupScrubbingOnError cleanup_on_error,
                        const vd::CleanupScrubbingOnSuccess cleanup_on_success)
{
    Volume* v = VolManager::get()->findVolume_(volName);
    v->applyScrubbingWork(scrub_work,
                          cleanup_on_error,
                          cleanup_on_success);
}


uint64_t
api::volumePotential(const vd::SCOMultiplier s,
                     const boost::optional<vd::TLogMultiplier>& t)
{
    return VolManager::get()->volumePotential(s,
                                              t);
}

uint64_t
api::volumePotential(const backend::Namespace& nspace)
{
    return VolManager::get()->volumePotential(nspace);
}

backend::BackendConnectionManagerPtr
api::backend_connection_manager()
{
    return VolManager::get()->getBackendConnectionManager();
}

void
api::setSyncIgnore(const vd::VolumeId& volName,
                   const uint64_t number_of_syncs_to_ignore,
                   const uint64_t maximum_time_to_ignore_syncs_in_seconds)
{
    return VolManager::get()->findVolume_(volName)->setSyncIgnore(number_of_syncs_to_ignore,
                                                                  maximum_time_to_ignore_syncs_in_seconds);
}

void
api::getSyncIgnore(const vd::VolumeId& volName,
                   uint64_t& number_of_syncs_to_ignore,
                   uint64_t& maximum_time_to_ignore_syncs_in_seconds)
{
    return VolManager::get()->findVolume_(volName)->getSyncIgnore(number_of_syncs_to_ignore,
                                                                  maximum_time_to_ignore_syncs_in_seconds);
}

void
api::setSCOMultiplier(const vd::VolumeId& volName,
                      const vd::SCOMultiplier sco_mult)
{
    VolManager::get()->findVolume_(volName)->setSCOMultiplier(sco_mult);
}

SCOMultiplier
api::getSCOMultiplier(const vd::VolumeId& volName)
{
    return VolManager::get()->findVolume_(volName)->getSCOMultiplier();
}

void
api::setTLogMultiplier(const vd::VolumeId& volName,
                       const boost::optional<vd::TLogMultiplier>& tlog_mult)
{
    VolManager::get()->findVolume_(volName)->setTLogMultiplier(tlog_mult);
}

boost::optional<TLogMultiplier>
api::getTLogMultiplier(const vd::VolumeId& volName)
{
    return VolManager::get()->findVolume_(volName)->getTLogMultiplier();
}

void
api::setSCOCacheMaxNonDisposableFactor(const vd::VolumeId& volName,
                                       const boost::optional<vd::SCOCacheNonDisposableFactor>& max_non_disposable_factor)
{
    VolManager::get()->findVolume_(volName)->setSCOCacheMaxNonDisposableFactor(max_non_disposable_factor);
}

boost::optional<SCOCacheNonDisposableFactor>
api::getSCOCacheMaxNonDisposableFactor(const vd::VolumeId& volName)
{
    return VolManager::get()->findVolume_(volName)->getSCOCacheMaxNonDisposableFactor();
}



// Local Variables: **
// mode: c++ **
// End: **