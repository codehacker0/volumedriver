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

#ifndef VFS_PYTHON_CLIENT_H_
#define VFS_PYTHON_CLIENT_H_

#include "ClusterId.h"
#include "ClusterRegistry.h"
#include "XMLRPC.h"
#include "XMLRPCStructs.h"
#include "CloneFileFlags.h"

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/python/dict.hpp>
#include <boost/python/list.hpp>
#include <boost/python/tuple.hpp>
#include <boost/shared_ptr.hpp>

#include <youtils/ConfigurationReport.h>
#include <youtils/IOException.h>
#include <youtils/UpdateReport.h>

#include <volumedriver/MetaDataBackendConfig.h>
#include <volumedriver/FailOverCacheConfig.h>

namespace volumedriverfs
{

namespace clienterrors
{

MAKE_EXCEPTION(PythonClientException, fungi::IOException);
MAKE_EXCEPTION(ObjectNotFoundException, fungi::IOException);
MAKE_EXCEPTION(InvalidOperationException, fungi::IOException);
MAKE_EXCEPTION(SnapshotNotFoundException, fungi::IOException);
MAKE_EXCEPTION(FileExistsException, fungi::IOException);
MAKE_EXCEPTION(InsufficientResourcesException, fungi::IOException);
MAKE_EXCEPTION(PreviousSnapshotNotOnBackendException, fungi::IOException);
MAKE_EXCEPTION(ConfigurationUpdateException, fungi::IOException);
MAKE_EXCEPTION(NodeNotReachableException, fungi::IOException);
MAKE_EXCEPTION(ClusterNotReachableException, fungi::IOException);
MAKE_EXCEPTION(ObjectStillHasChildrenException, fungi::IOException);

class MaxRedirectsExceededException
    : public PythonClientException
{
public:
    MaxRedirectsExceededException(const char* method,
                                  const std::string& host,
                                  const uint16_t port)
       : PythonClientException(method)
       , method(method)
       , host(host)
       , port(port)
    {}

    const std::string method;
    const std::string host;
    const uint16_t port;
};

}

struct ClusterContact
{
    ClusterContact(const std::string& host,
                   const uint16_t port)
        : host(host)
        , port(port)
    {}

    const std::string host;
    const uint16_t port;
};

class PythonClient
{
private:
    static constexpr unsigned max_redirects_default = 2;

public:
    // max_redirects should only be changed from its default in the testers
    // or by the LocalPythonClient

    PythonClient(const std::string& cluster_id,
                 const std::vector<ClusterContact>& cluster_contacts,
                 const unsigned max_redirects = max_redirects_default);

    virtual ~PythonClient() = default;

    boost::python::list
    list_volumes();

    boost::python::list
    list_snapshots(const std::string& volume_id);

    XMLRPCSnapshotInfo
    info_snapshot(const std::string& volume_id,
                  const std::string& snapshot_id);

    XMLRPCVolumeInfo
    info_volume(const std::string& volume_id);

    XMLRPCStatistics
    statistics_volume(const std::string& volume_id,
                      bool reset = false);

    std::string
    create_snapshot(const std::string& volume_id,
                    const std::string& snapshot_id = "",
                    const std::string& metadata = "");

    std::string
    create_volume(const std::string& target_path,
                  boost::shared_ptr<volumedriver::MetaDataBackendConfig> mdb_config,
                  const std::string& volume_size,
                  const std::string& node_id = "");

    std::string
    create_clone(const std::string& target_path,
                 boost::shared_ptr<volumedriver::MetaDataBackendConfig> mdb_config,
                 const std::string& parent_volume_id,
                 const std::string& parent_snap_id,
                 const std::string& node_id = "");

    void
    update_metadata_backend_config(const std::string& volume_id,
                                   boost::shared_ptr<volumedriver::MetaDataBackendConfig> mdb_config);

    std::string
    create_clone_from_template(const std::string& target_path,
                               boost::shared_ptr<volumedriver::MetaDataBackendConfig> mdb_config,
                               const std::string& parent_volume_id,
                               const std::string& node_id = "");

    void
    rollback_volume(const std::string& volume_id,
                    const std::string& snapshot_id);

    void
    delete_snapshot(const std::string& volume_id,
                    const std::string& snapshot_id);

    void
    set_volume_as_template(const std::string& vname);

    boost::python::list
    get_scrubbing_work(const std::string& volume_id);

    void
    apply_scrubbing_result(const boost::python::tuple& tuple);

    std::string
    server_revision();

    std::string
    client_revision();

    void
    migrate(const std::string& object_id,
            const std::string& node_id,
            bool force = false);

    void
    mark_node_offline(const std::string& node_id);

    void
    mark_node_online(const std::string& node_id);

    uint64_t
    volume_potential(const std::string& node_id);


    std::map<NodeId, ClusterNodeStatus::State>
    info_cluster();

    boost::optional<volumedriver::VolumeId>
    get_volume_id(const std::string& path);

    boost::python::dict
    get_sync_ignore(const std::string& volume_id);


    void
    set_sync_ignore(const std::string& volume_id,
                    uint64_t number_of_syncs_to_ignore,
                    uint64_t maximum_time_to_ignore_syncs_in_seconds);

    uint32_t
    get_sco_multiplier(const std::string& volume_id);


    void
    set_sco_multiplier(const std::string& volume_id,
                       uint32_t sco_multiplier);

    boost::optional<uint32_t>
    get_tlog_multiplier(const std::string& volume_id);


    void
    set_tlog_multiplier(const std::string& volume_id,
                        const boost::optional<uint32_t>& tlog_multiplier);

    boost::optional<float>
    get_sco_cache_max_non_disposable_factor(const std::string& volume_id);


    void
    set_sco_cache_max_non_disposable_factor(const std::string& volume_id,
                                            const boost::optional<float>& max_non_disposable_factor);

    FailOverCacheConfigMode
    get_failover_cache_config_mode(const std::string& volume_id);

    boost::optional<volumedriver::FailOverCacheConfig>
    get_failover_cache_config(const std::string& volume_id);

    void
    set_manual_failover_cache_config(const std::string& volume_id,
                                     const boost::optional<volumedriver::FailOverCacheConfig>& foc_config);

    void
    set_automatic_failover_cache_config(const std::string& volume_id);

    boost::optional<volumedriver::ClusterCacheBehaviour>
    get_cluster_cache_behaviour(const std::string& volume_id);

    void
    set_cluster_cache_behaviour(const std::string& volume_id,
                                const boost::optional<volumedriver::ClusterCacheBehaviour>&);

    boost::optional<volumedriver::ClusterCacheMode>
    get_cluster_cache_mode(const std::string& volume_id);

    void
    set_cluster_cache_mode(const std::string& volume_id,
                           const boost::optional<volumedriver::ClusterCacheMode>&);

    void
    set_cluster_cache_limit(const std::string& volume_id,
                            const boost::optional<volumedriver::ClusterCount>&
                            cluster_cache_limit);

    boost::optional<volumedriver::ClusterCount>
    get_cluster_cache_limit(const std::string& volume_id);

    void
    update_cluster_node_configs(const std::string& node_id);

    void
    vaai_copy(const std::string& src_path,
              const std::string& target_path,
              const uint64_t& timeout,
              const CloneFileFlags& flags);

protected:
    PythonClient()
        : max_redirects(max_redirects_default)
    {}

    XmlRpc::XmlRpcValue
    call(const char* method,
         XmlRpc::XmlRpcValue& req);

    XmlRpc::XmlRpcValue
    redirected_xmlrpc(const std::string& addr,
                      const uint16_t port,
                      const char* method,
                      XmlRpc::XmlRpcValue& req,
                      unsigned& redirect_count);

    std::string cluster_id_;
    //this list does not necessarily contain all nodes in the cluster
    std::vector<ClusterContact> cluster_contacts_;

private:
    DECLARE_LOGGER("PythonClient");

public:
    const unsigned max_redirects;
};

}

#endif // !VFS_PYTHON_CLIENT_H_