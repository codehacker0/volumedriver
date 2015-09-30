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

#ifndef API_H_
#define API_H_

// currently the api leaks too many implementation details - this needs to be
// revised

#include "ClusterCount.h"
#include "Events.h"
#include "FailOverCacheConfig.h"
#include "MetaDataStoreStats.h"
#include "OwnerTag.h"
#include "PerformanceCounters.h"
#include "Volume.h"
#include "VolumeConfig.h"
#include "VolumeConfigParameters.h"
#include "SCOCacheInfo.h"
#include "Snapshot.h"
#include "ClusterCache.h"
#include "VolumeOverview.h"
#include "Types.h"

#include <string>
#include <limits>
#include <list>

#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/variant.hpp>

#include <youtils/ArakoonNodeConfig.h>
#include <youtils/Assert.h>
#include <youtils/ConfigurationReport.h>
#include <youtils/UpdateReport.h>
#include <youtils/UUID.h>

#include <backend/Namespace.h>

namespace volumedriver
{

class Volume;
class WriteOnlyVolume;
class MetaDataBackendConfig;

}

class api
{
public:
    static fungi::Mutex&
    getManagementMutex();

    static void
    createNewVolume(const volumedriver::VanillaVolumeConfigParameters& p,
                    const volumedriver::CreateNamespace = volumedriver::CreateNamespace::F);

    static volumedriver::WriteOnlyVolume*
    createNewWriteOnlyVolume(const volumedriver::WriteOnlyVolumeConfigParameters&);

    static void
    createClone(const volumedriver::CloneVolumeConfigParameters&,
                const volumedriver::PrefetchVolumeData,
                const volumedriver::CreateNamespace = volumedriver::CreateNamespace::F);

    static void
    updateMetaDataBackendConfig(volumedriver::Volume*,
                                const volumedriver::MetaDataBackendConfig&);

    static void
    updateMetaDataBackendConfig(const volumedriver::VolumeId&,
                                const volumedriver::MetaDataBackendConfig&);

    static volumedriver::Volume*
    getVolumePointer(const volumedriver::VolumeId&);

    static volumedriver::Volume*
    get_volume_pointer_no_throw(const volumedriver::VolumeId&);

    static volumedriver::Volume*
    getVolumePointer(const volumedriver::Namespace&);

    static const volumedriver::VolumeId
    getVolumeId(volumedriver::Volume*);

    static void
    Write(volumedriver::Volume* vol,
          uint64_t lba,
          const uint8_t *buf,
          uint64_t buflen);

    static void
    Write(volumedriver::WriteOnlyVolume* vol,
          const uint64_t lba,
          const uint8_t *buf,
          const uint64_t buflen);

    static void
    Read(volumedriver::Volume* vol,
         const uint64_t lba,
         uint8_t *buf,
         const uint64_t buflen);

    static void
    Sync(volumedriver::Volume* vol);

    static void
    Resize(volumedriver::Volume* vol,
           uint64_t num_clusters);

    static uint64_t
    GetSize(volumedriver::Volume* vol);

    static uint64_t
    GetLbaSize(volumedriver::Volume* vol);

    static void
    Init(const boost::filesystem::path& cfg,
         events::PublisherPtr event_publisher = nullptr);

    static void
    Init(const boost::property_tree::ptree& pt,
         events::PublisherPtr event_publisher = nullptr);

    // this one is the volumedriver's "cosmological constant", i.e. it's not configurable
    // per volume anymore as things like the cluster cache also rely on it.
    static uint64_t
    GetClusterSize();

    static void
    Exit(void);

    static void
    getVolumeList(std::list<volumedriver::VolumeId>&);

    static void
    getVolumesOverview(std::map<volumedriver::VolumeId,
                       volumedriver::VolumeOverview>& out_map);

    static volumedriver::VolumeConfig
    getVolumeConfig(const volumedriver::VolumeId& volName);

    static void
    local_restart(const volumedriver::Namespace&,
                  const volumedriver::OwnerTag,
                  const volumedriver::FallBackToBackendRestart,
                  const volumedriver::IgnoreFOCIfUnreachable,
                  const uint32_t num_pages_cached);

    static void
    backend_restart(const volumedriver::Namespace&,
                    const volumedriver::OwnerTag,
                    const volumedriver::PrefetchVolumeData,
                    const volumedriver::IgnoreFOCIfUnreachable,
                    const uint32_t num_pages_cached);

    static volumedriver::WriteOnlyVolume*
    restartVolumeWriteOnly(const volumedriver::Namespace& ns,
                           const volumedriver::OwnerTag);

    // The destroyVolume flavours MUST NOT be invoked concurrently for the same
    // volume; they can however be invoked concurrently for different volumes.
    static void
    destroyVolume(const volumedriver::VolumeId& volName,
                  const volumedriver::DeleteLocalData delete_local_data,
                  const volumedriver::RemoveVolumeCompletely remove_volume_completely,
                  const volumedriver::DeleteVolumeNamespace delete_volume_namespace,
                  const volumedriver::ForceVolumeDeletion force_volume_deletion);

    static void
    destroyVolume(volumedriver::Volume* volName,
                  const volumedriver::DeleteLocalData delete_local_data,
                  const volumedriver::RemoveVolumeCompletely remove_volume_completely,
                  const volumedriver::DeleteVolumeNamespace delete_volume_namespace,
                  const volumedriver::ForceVolumeDeletion force_volume_deletion);

    static void
    destroyVolume(volumedriver::WriteOnlyVolume* vol,
                  const volumedriver::RemoveVolumeCompletely);

    static void
    setAsTemplate(const volumedriver::VolumeId&);

    static void
    removeLocalVolumeData(const backend::Namespace& nspace);

    static std::string
    createSnapshot(volumedriver::Volume*,
                   const volumedriver::SnapshotMetaData& = volumedriver::SnapshotMetaData(),
                   const std::string* const snapname = 0,
                   const volumedriver::UUID& uuid = volumedriver::UUID());

    static std::string
    createSnapshot(const volumedriver::VolumeId&,
                   const volumedriver::SnapshotMetaData& = volumedriver::SnapshotMetaData(),
                   const std::string* const snapname = 0);

    static std::string
    createSnapshot(volumedriver::WriteOnlyVolume*,
                   const volumedriver::SnapshotMetaData& = volumedriver::SnapshotMetaData(),
                   const std::string* const snapname = 0,
                   const volumedriver::UUID& uuid = volumedriver::UUID());

    static bool
    checkSnapshotUUID(const volumedriver::WriteOnlyVolume*,
                      const std::string& snapshotName,
                      const volumedriver::UUID& uuid);

    static bool
    snapshotExists(const volumedriver::WriteOnlyVolume*,
                   const std::string& snapshotName);

    static void
    showSnapshots(const volumedriver::VolumeId&,
                  std::list<std::string>& l);

    static void
    showSnapshots(const volumedriver::WriteOnlyVolume*,
                  std::list<std::string>& l);

    static volumedriver::Snapshot
    getSnapshot(const volumedriver::VolumeId&,
                const std::string& snapname);

    static volumedriver::Snapshot
    getSnapshot(const volumedriver::Volume*,
                const std::string& snapname);

    static volumedriver::Snapshot
    getSnapshot(const volumedriver::WriteOnlyVolume*,
                const std::string& snapname);

    static void
    destroySnapshot(const volumedriver::VolumeId&,
                     const std::string& snapName);

    static void
    restoreSnapshot(const volumedriver::VolumeId&,
                    const std::string& snapid);

    static void
    restoreSnapshot(volumedriver::WriteOnlyVolume*,
                    const std::string& snapid);

    static uint64_t
    VolumeDataStoreWriteUsed(const volumedriver::VolumeId&);

    static uint64_t
    VolumeDataStoreReadUsed(const volumedriver::VolumeId&);

    static volumedriver::PerformanceCounters&
    performance_counters(const volumedriver::VolumeId& volName);

    static uint64_t
    getCacheHits(const volumedriver::VolumeId&);

    static uint64_t
    getCacheMisses(const volumedriver::VolumeId&);

    static uint64_t
    getNonSequentialReads(const volumedriver::VolumeId& volName);

    static uint64_t
    getClusterCacheHits(const volumedriver::VolumeId&);

    static uint64_t
    getClusterCacheMisses(const volumedriver::VolumeId&);

    static uint64_t
    getQueueCount(const volumedriver::VolumeId&);

    static uint64_t
    getQueueSize(const volumedriver::VolumeId& volName);

    static uint64_t
    getTLogUsed(const volumedriver::VolumeId&);

    static void
    scheduleBackendSync(const volumedriver::VolumeId&);

    static uint32_t
    getMaxTimeBetweenTLogs();

    static uint32_t
    getNumberOfSCOSBetweenCheck();

    static void
    setMaxTimeBetweenTLogs(uint32_t max_time_between_tlogs);

    static bool
    isVolumeSynced(const volumedriver::VolumeId&);

    static bool
    isVolumeSyncedUpTo(const volumedriver::VolumeId&,
                       const std::string& snapshotName);

    static bool
    isVolumeSyncedUpTo(const volumedriver::WriteOnlyVolume* v,
                       const std::string& snapshotName);

    static void
    removeNamespaceFromSCOCache(const backend::Namespace&);

    static std::string
    getRevision();

    static void
    getSCOCacheMountPointsInfo(volumedriver::SCOCacheMountPointsInfo& info);

    static volumedriver::SCOCacheNamespaceInfo
    getVolumeSCOCacheInfo(const backend::Namespace);

    static void
    getClusterCacheDeviceInfo(volumedriver::ClusterCache::ManagerType::Info& info);

    static volumedriver::ClusterCacheVolumeInfo
    getClusterCacheVolumeInfo(const volumedriver::VolumeId& volName);

    static volumedriver::VolumeFailoverState
    getFailOverMode(const volumedriver::VolumeId&);

    static void
    setFailOverCacheConfig(const volumedriver::VolumeId&,
                           const boost::optional<volumedriver::FailOverCacheConfig>&);

    static boost::optional<volumedriver::FailOverCacheConfig>
    getFailOverCacheConfig(const volumedriver::VolumeId& volname);

    static uint64_t
    getCurrentSCOCount(const volumedriver::VolumeId&);

    static bool
    getHalted(const volumedriver::VolumeId&);

    static uint64_t
    getSnapshotSCOCount(const volumedriver::VolumeId&,
                        const std::string& snapid);

    static void
    setFOCTimeout(const volumedriver::VolumeId& volName,
                  uint32_t timeout);

    static bool
    checkVolumeConsistency(const volumedriver::VolumeId&);

    static uint64_t
    getSnapshotBackendSize(const volumedriver::VolumeId&,
                            const std::string& snapName);

    static uint64_t
    getCurrentBackendSize(const volumedriver::VolumeId&);

    static bool
    volumeExists(const volumedriver::VolumeId&);

    static void
    dumpSCOInfo(const std::string& outfile);

    static void
    addSCOCacheMountPoint(const std::string& path,
                          uint64_t size);

    static void
    removeSCOCacheMountPoint(const std::string& path);

    static void
    addClusterCacheDevice(const std::string& path,
                       const uint64_t size);

    static void
    removeClusterCacheDevice(const std::string& path);

    static void
    setClusterCacheDeviceOffline(const std::string& path);

    static void
    setClusterCacheDeviceOnline(const std::string& path);

    static boost::variant<youtils::UpdateReport,
                          youtils::ConfigurationReport>
    updateConfiguration(const boost::property_tree::ptree&);

    static boost::variant<youtils::UpdateReport,
                          youtils::ConfigurationReport>
    updateConfiguration(const boost::filesystem::path&);

    static void
    persistConfiguration(const boost::filesystem::path&,
                         const bool reportDefault);

    static std::string
    persistConfiguration(const bool reportDefault);

    static void
    persistConfiguration(boost::property_tree::ptree&,
                         const bool reportDefault);

    static bool
    checkConfiguration(const boost::filesystem::path&,
                       volumedriver::ConfigurationReport& rep);

    static void
    startRecording();

    static void
    stopRecording();

    static void
    startPrefetching(const volumedriver::VolumeId& volName);

    static void
    getClusterCacheStats(uint64_t& num_hits,
                         uint64_t& num_misses,
                         uint64_t& num_entries);

    static volumedriver::MetaDataStoreStats
    getMetaDataStoreStats(const volumedriver::VolumeId& volname);

    static uint64_t
    getStored(const volumedriver::VolumeId& volName);

    static bool
    getVolumeDriverReadOnly();

    static void
    setClusterCacheBehaviour(const volumedriver::VolumeId&,
                             const boost::optional<volumedriver::ClusterCacheBehaviour>);

    static boost::optional<volumedriver::ClusterCacheBehaviour>
    getClusterCacheBehaviour(const volumedriver::VolumeId&);

    static void
    setClusterCacheMode(const volumedriver::VolumeId&,
                        const boost::optional<volumedriver::ClusterCacheMode>&);

    static boost::optional<volumedriver::ClusterCacheMode>
    getClusterCacheMode(const volumedriver::VolumeId&);

    static void
    setClusterCacheLimit(const volumedriver::VolumeId&,
                         const boost::optional<volumedriver::ClusterCount>&);

    static boost::optional<volumedriver::ClusterCount>
    getClusterCacheLimit(const volumedriver::VolumeId&);

    static void
    getScrubbingWork(const volumedriver::VolumeId&,
                     std::vector<std::string>& scrubbing_work_units,
                     const boost::optional<std::string>& start_snap,
                     const boost::optional<std::string>& end_snap);

    static void
    applyScrubbingWork(const volumedriver::VolumeId&,
                       const std::string& scrub_work,
                       const volumedriver::CleanupScrubbingOnError = volumedriver::CleanupScrubbingOnError::F,
                       const volumedriver::CleanupScrubbingOnSuccess = volumedriver::CleanupScrubbingOnSuccess::T);

    static uint64_t
    volumePotential(const volumedriver::SCOMultiplier,
                    const boost::optional<volumedriver::TLogMultiplier>&);

    static uint64_t
    volumePotential(const backend::Namespace&);

    static backend::BackendConnectionManagerPtr
    backend_connection_manager();

    static void
    setSyncIgnore(const volumedriver::VolumeId& volName,
                  const uint64_t number_of_syncs_to_ignore,
                  const uint64_t maximum_time_to_ignore_syncs_in_seconds);


    static void
    getSyncIgnore(const volumedriver::VolumeId& volName,
                       uint64_t& number_of_syncs_to_ignore,
                       uint64_t& maximum_time_to_ignore_syncs_in_seconds);

    static void
    setSCOMultiplier(const volumedriver::VolumeId& volName,
                     const volumedriver::SCOMultiplier sco_mult);

    static volumedriver::SCOMultiplier
    getSCOMultiplier(const volumedriver::VolumeId& volName);

    static void
    setTLogMultiplier(const volumedriver::VolumeId& volName,
                      const boost::optional<volumedriver::TLogMultiplier>& tlog_mult);

    static boost::optional<volumedriver::TLogMultiplier>
    getTLogMultiplier(const volumedriver::VolumeId& volName);

    static void
    setSCOCacheMaxNonDisposableFactor(const volumedriver::VolumeId& volName,
                                      const boost::optional<volumedriver::SCOCacheNonDisposableFactor>& max_non_disposable_factor);

    static boost::optional<volumedriver::SCOCacheNonDisposableFactor>
    getSCOCacheMaxNonDisposableFactor(const volumedriver::VolumeId& volName);

};

#endif // API_H_

// Local Variables: **
// mode: c++ **
// End: **