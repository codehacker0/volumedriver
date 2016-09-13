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

#ifndef VFS_TEST_SETUP_H_
#define VFS_TEST_SETUP_H_

#include "FailOverCacheTestHelper.h"

#include <youtils/ArakoonTestSetup.h>
#include <youtils/Logging.h>
#include <gtest/gtest.h>

#include <backend/BackendTestSetup.h>

#include <volumedriver/FailOverCacheTransport.h>

#include <filesystem/ClusterId.h>
#include <filesystem/ClusterNodeConfig.h>
#include <filesystem/NodeId.h>
#include <filesystem/PythonClient.h>
#include <filesystem/VirtualDiskFormat.h>

namespace metadata_server
{

class Manager;
class ServerConfig;

}

namespace volumedrivertest
{
class MDSTestSetup;
class MetaDataStoreTestSetup;
}

namespace volumedriverfstest
{

struct FileSystemTestSetupParameters
{
    explicit FileSystemTestSetupParameters(const std::string& name)
        : name_(name)
    {}

    const std::string name_;

#define PARAM(typ, name)                         \
    FileSystemTestSetupParameters&               \
    name(const typ& t)                           \
    {                                            \
        name ## _ = t;                           \
        return *this;                            \
    }                                            \
                                                 \
    typ name ## _

    PARAM(unsigned, num_threads) = 4;
    PARAM(std::string, scocache_size) = "1GiB";
    PARAM(std::string, scocache_trigger_gap) = "250MiB";
    PARAM(std::string, scocache_backoff_gap) = "500MiB";
    PARAM(uint32_t, scocache_clean_interval) = 60;
    PARAM(unsigned, open_scos_per_volume) = 32;
    PARAM(unsigned, datastore_throttle_usecs) = 4000;
    PARAM(unsigned, failovercache_throttle_usecs) = 10000;
    PARAM(uint32_t, scos_per_tlog) = 20;
    PARAM(uint64_t, backend_sync_timeout_ms) = 0;
    PARAM(uint64_t, migrate_timeout_ms) = 0;
    PARAM(uint64_t, redirect_timeout_ms) = 0;
    PARAM(uint64_t, redirect_retries) = 2;
    PARAM(uint64_t, scrub_manager_interval_secs) = 1;
    PARAM(bool, use_fencing) = false;

#undef PARAM
};

class FileSystemTestSetup
    : public testing::Test
    , public backend::BackendTestSetup
{
public:
    virtual ~FileSystemTestSetup();

    static void
    virtual_disk_format(std::unique_ptr<volumedriverfs::VirtualDiskFormat> fmt)
    {
        vdisk_format_.reset(fmt.release());
    }

    static uint16_t
    port_base();

    static boost::filesystem::path
    make_volume_name(const std::string& s);

    static std::string
    address()
    {
        return address_;
    }

    static volumedriver::FailOverCacheTransport
    failovercache_transport()
    {
        return failovercache_transport_;
    }

    static const std::string
    edge_transport()
    {
        return edge_transport_;
    }

    static std::string address_;
    static volumedriver::FailOverCacheTransport failovercache_transport_;
    static std::string edge_transport_;

protected:
    FileSystemTestSetup(const FileSystemTestSetupParameters&);

    void
    start_failovercache_for_local_node();

    void
    start_failovercache_for_remote_node();

    void
    stop_failovercache_for_local_node();

    void
    stop_failovercache_for_remote_node();

    void
    put_cluster_node_configs_in_registry(const boost::property_tree::ptree& pt);

    void
    setup_clustercache_device(const boost::filesystem::path& device,
                              uint64_t size) const;

    void
    maybe_setup_clustercache_device(const boost::filesystem::path& device,
                                    uint64_t size) const;

    virtual void
    SetUp();

    virtual void
    TearDown();

    boost::property_tree::ptree&
    make_mdstore_config_(boost::property_tree::ptree& pt) const;

    boost::property_tree::ptree&
    make_config_(boost::property_tree::ptree& pt,
                 const boost::filesystem::path& topdir,
                 const volumedriverfs::NodeId& vrouter_id) const;

    boost::property_tree::ptree&
    make_registry_config_(boost::property_tree::ptree&) const;

    static boost::property_tree::ptree&
    make_edge_config_(boost::property_tree::ptree& pt,
                      const volumedriverfs::NodeId& vrouter_id);

    void
    make_dirs_(const boost::filesystem::path& topdir) const;

    boost::filesystem::path
    source_dir() const
    {
        return topdir_ / "src";
    }

    static boost::filesystem::path
    mount_dir(const boost::filesystem::path& topdir)
    {
        return topdir / "mnt";
    }

    static boost::filesystem::path
    remote_dir(const boost::filesystem::path& topdir)
    {
        return topdir / "remote";
    }

    static boost::filesystem::path
    scocache_mountpoint(const boost::filesystem::path& topdir)
    {
        const boost::filesystem::path scache(topdir / "scache");
        return scache / "0";
    }

    static boost::filesystem::path
    clustercache_dir(const boost::filesystem::path& topdir)
    {
        return topdir / "ccache";
    }

    static boost::filesystem::path
    clustercache_mountpoint(const boost::filesystem::path& topdir)
    {
        return clustercache_dir(topdir) / "0";
    }

    static boost::filesystem::path
    clustercache_serialization_path(const boost::filesystem::path& topdir)
    {
        return clustercache_dir(topdir) / "readcache_serialization";
    }

    static boost::filesystem::path
    tlog_dir(const boost::filesystem::path& topdir)
    {
        return topdir / "tlogs";
    }

    static boost::filesystem::path
    mdstore_dir(const boost::filesystem::path& topdir)
    {
        return topdir / "meta";
    }

    static boost::filesystem::path
    mds_dir(const boost::filesystem::path& topdir)
    {
        return topdir / "mds";
    }

    static boost::filesystem::path
    dump_on_halt_dir(const boost::filesystem::path& topdir)
    {
        return topdir / "dump_on_halt";
    }

    static boost::filesystem::path
    failovercache_dir(const boost::filesystem::path& topdir)
    {
        return topdir / "failovercache";
    }

    static volumedriverfs::ClusterId
    vrouter_cluster_id()
    {
        return volumedriverfs::ClusterId("volumedriverfs-cluster");
    }

    static volumedriverfs::NodeId
    local_node_id()
    {
        return volumedriverfs::NodeId("node_0");
    }

    static volumedriverfs::NodeId
    remote_node_id()
    {
        return volumedriverfs::NodeId("node_1");
    }

    static volumedriverfs::ClusterNodeConfig
    local_config()
    {
        return volumedriverfs::ClusterNodeConfig(local_node_id(),
                                                 address(),
                                                 volumedriverfs::MessagePort(port_base() + 1),
                                                 volumedriverfs::XmlRpcPort(port_base() + 3),
                                                 volumedriverfs::FailoverCachePort(port_base() + 5));
    }

    static volumedriverfs::ClusterNodeConfig
    remote_config()
    {
        return volumedriverfs::ClusterNodeConfig(remote_node_id(),
                                                 address(),
                                                 volumedriverfs::MessagePort(port_base() + 2),
                                                 volumedriverfs::XmlRpcPort(port_base() + 4),
                                                 volumedriverfs::FailoverCachePort(port_base() + 6));
    }

    static uint16_t
    local_edge_port()
    {
        return port_base() + 7;
    }

    static uint16_t
    remote_edge_port()
    {
        return port_base() + 8;
    }

    static volumedriverfs::ClusterNodeConfigs
    cluster_node_configs()
    {
        const volumedriverfs::ClusterNodeConfigs configs = { local_config(),
                                                             remote_config() };
        return configs;
    }

    static boost::filesystem::path
    fdriver_cache_dir(const boost::filesystem::path& topdir)
    {
        return topdir / "fcache";
    }

    volumedriver::VolumeConfig::MetaDataBackendConfigPtr
    make_metadata_backend_config();

    static std::unique_ptr<volumedriverfs::VirtualDiskFormat> vdisk_format_;

    const std::string test_name_;
    const boost::filesystem::path topdir_;
    const boost::filesystem::path configuration_;

    const uint64_t scache_size_;
    const uint64_t scache_trigger_gap_;
    const uint64_t scache_backoff_gap_;
    const uint32_t scache_clean_interval_;

    unsigned open_scos_per_volume_;
    unsigned dstore_throttle_usecs_;
    unsigned foc_throttle_usecs_;

    const static uint tlog_max_entries_ = 58254;
    const unsigned num_threads_;
    const uint32_t num_scos_in_tlog_;
    uint64_t backend_sync_timeout_ms_;
    uint64_t migrate_timeout_ms_;
    uint64_t redirect_timeout_ms_;
    uint32_t redirect_retries_;
    uint64_t scrub_manager_interval_secs_;
    bool use_fencing_;

    const backend::Namespace fdriver_namespace_;

    std::unique_ptr<FailOverCacheTestHelper> local_foc_helper_;
    std::unique_ptr<FailOverCacheTestHelper> remote_foc_helper_;

    std::shared_ptr<arakoon::ArakoonTestSetup> arakoon_test_setup_;
    std::shared_ptr<volumedrivertest::MDSTestSetup> mds_test_setup_;
    std::unique_ptr<metadata_server::ServerConfig> mds_server_config_;
    std::unique_ptr<volumedrivertest::MetaDataStoreTestSetup> mdstore_test_setup_;
    std::unique_ptr<metadata_server::Manager> mds_manager_;

    volumedriverfs::PythonClient client_;

private:
    DECLARE_LOGGER("FileSystemTestSetup");
};

}

#endif // !VFS_TEST_SETUP_H_
