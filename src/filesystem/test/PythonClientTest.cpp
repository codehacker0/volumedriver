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

#include "FileSystemTestBase.h"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/python/extract.hpp>

#include <xmlrpc++0.7/src/XmlRpcClient.h>

#include <youtils/Catchers.h>
#include <youtils/IOException.h>
#include <youtils/InitializedParam.h>
#include <youtils/FileDescriptor.h>
#include <youtils/ScopeExit.h>

#include <volumedriver/metadata-server/Manager.h>
#include <volumedriver/SnapshotPersistor.h>
#include <volumedriver/Volume.h>
#include <volumedriver/ScrubberAdapter.h>
#include <volumedriver/test/MDSTestSetup.h>

#include "../PythonClient.h"
#include "../ObjectRouter.h"
#include "../Registry.h"
#include "../XMLRPC.h"
#include "../XMLRPCKeys.h"
#include "../XMLRPCStructs.h"
#include "../CloneFileFlags.h"

namespace volumedriverfstest
{

namespace bpt = boost::property_tree;
namespace bpy = boost::python;
namespace fs = boost::filesystem;
namespace ip = initialized_params;
namespace mds = metadata_server;
namespace vd = volumedriver;
namespace vfs = volumedriverfs;
namespace yt = youtils;

using namespace std::literals::string_literals;

#define LOCKVD()                                                        \
    std::lock_guard<fungi::Mutex> vdg__(api::getManagementMutex())

namespace
{

struct SomeRedirects
    : public ::XmlRpc::XmlRpcServerMethod
{
    SomeRedirects(unsigned num_redirects,
                  const std::string& name,
                  uint16_t redir_port)
        : ::XmlRpc::XmlRpcServerMethod(name, nullptr)
        , max_redirects(num_redirects)
        , redirects(0)
        , port(redir_port)
    {}

    virtual ~SomeRedirects()
    {}

    const unsigned max_redirects;
    unsigned redirects;
    const uint16_t port;

    virtual void
    execute(::XmlRpc::XmlRpcValue& /* params */,
            ::XmlRpc::XmlRpcValue& result)
    {
        result.clear();

        if (redirects++ < max_redirects)
        {
            result[vfs::XMLRPCKeys::xmlrpc_redirect_host] =
                FileSystemTestSetup::address();
            result[vfs::XMLRPCKeys::xmlrpc_redirect_port] =
                boost::lexical_cast<std::string>(port);
        }
    }
};

}

class PythonClientTest
    : public FileSystemTestBase
{
protected:
    PythonClientTest()
        : FileSystemTestBase(FileSystemTestSetupParameters("PythonClientTest"))
        , client_(vrouter_cluster_id(),
                  {{address(), local_config().xmlrpc_port}})
    {
        Py_Initialize();
    }

    virtual
    ~PythonClientTest()
    {
        Py_Finalize();
    }

    DECLARE_LOGGER("PythonClientTest");

    void
    check_snapshot(const std::string& vname,
                   const std::string& sname,
                   const std::string& meta = "")
    {
        vfs::XMLRPCSnapshotInfo snap_info(client_.info_snapshot(vname, sname));

        EXPECT_TRUE(sname == snap_info.snapshot_id);
        EXPECT_TRUE(meta == snap_info.metadata);
    }

    void
    test_redirects(unsigned max_redirects)
    {
        // we can reuse the remote xmlrpc port as we won't spawn another fs instance
        const uint16_t port = static_cast<uint16_t>(remote_config().xmlrpc_port);
        xmlrpc::Server srv(address(),
                           port);

        std::unique_ptr<::XmlRpc::XmlRpcServerMethod>
            method(new SomeRedirects(max_redirects,
                                     "snapshotDestroy",
                                     port));

        srv.addMethod(std::move(method));
        srv.start();

        BOOST_SCOPE_EXIT((&srv))
        {
            srv.stop();
            srv.join();
        }
        BOOST_SCOPE_EXIT_END;

        vfs::PythonClient remoteclient(vrouter_cluster_id(),
                                       {{address(), port}});
        const vfs::ObjectId id(yt::UUID().str());
        const std::string snap("snapshot");

        if (max_redirects > client_.max_redirects)
        {
            EXPECT_THROW(remoteclient.delete_snapshot(id, snap),
                         vfs::clienterrors::MaxRedirectsExceededException);
        }
        else
        {
            EXPECT_NO_THROW(remoteclient.delete_snapshot(id, snap));
        }
    }

    bpy::tuple
    scrub_wrap(const bpy::object& work_item)
    {
        auto cpp_result = scrubber_.scrub_(std::string(bpy::extract<std::string>(work_item)), "/tmp");
        bpy::tuple py_result = boost::python::make_tuple(cpp_result.first, cpp_result.second);
        return py_result;
    }

    void
    check_foc_config(const std::string& vname,
                     const vfs::FailOverCacheConfigMode exp_mode,
                     const boost::optional<vd::FailOverCacheConfig>& exp_config)
    {
        EXPECT_EQ(exp_mode,
                  client_.get_failover_cache_config_mode(vname));
        EXPECT_EQ(exp_config,
                  client_.get_failover_cache_config(vname));
    }

    void
    check_foc_state(const std::string& vname,
                    const vd::VolumeFailoverState state)
    {
        const vfs::XMLRPCVolumeInfo info(client_.info_volume(vname));
        EXPECT_EQ(state,
                  boost::lexical_cast<vd::VolumeFailoverState>(info.failover_mode));
    }

    vd::FailOverCacheConfig
    check_initial_foc_config(const std::string& vname)
    {
        const vd::FailOverCacheConfig cfg(remote_config().host,
                                          remote_config().failovercache_port);
        check_foc_config(vname,
                         vfs::FailOverCacheConfigMode::Automatic,
                         cfg);

        return cfg;
    }

    vfs::PythonClient client_;
    scrubbing::ScrubberAdapter scrubber_;
};

// Oh yeah, without Py_Initialize (and its Py_Finalize counterpart) even things
// as simple as that won't work.
TEST_F(PythonClientTest, DISABLED_pylist)
{
    bpy::list l;
    EXPECT_EQ(0, bpy::len(l));

    const std::string s("foo");
    l.append(s);

    EXPECT_EQ(1, bpy::len(l));
    const std::string t = bpy::extract<std::string>(l[0]);
    EXPECT_EQ(s, t);
}

TEST_F(PythonClientTest, no_one_listening)
{
    // there should not be anyone listening on the remote XMLRPC port
    vfs::PythonClient c(vrouter_cluster_id(),
                        {{address(), remote_config().xmlrpc_port}});
    EXPECT_THROW(c.list_volumes(), vfs::clienterrors::ClusterNotReachableException);
}

TEST_F(PythonClientTest, wrong_cluster_id)
{
    // there should not be anyone listening on the remote XMLRPC port
    vfs::PythonClient c("non-existing-cluster-id",
                        {{address(),
                          local_config().xmlrpc_port}});
    EXPECT_THROW(c.list_volumes(),
                 vfs::clienterrors::PythonClientException);
}

TEST_F(PythonClientTest, list_volumes)
{
    const vfs::ObjectId fid(create_file(vfs::FrontendPath("/some-file"s),
                                        4096));

    {
        const bpy::list l(client_.list_volumes());
        EXPECT_EQ(0, bpy::len(l));
    }

    std::set<std::string> volumes;
    const uint32_t num_volumes = 2;

    for (uint32_t i = 0; i < num_volumes; ++i)
    {
        const vfs::FrontendPath
            vpath(make_volume_name("/vol-"s +
                                   boost::lexical_cast<std::string>(i)));
        const vfs::ObjectId vname(create_file(vpath, 10 << 20));
        const auto res(volumes.insert(vname));
        EXPECT_TRUE(res.second);
    }

    const bpy::list l(client_.list_volumes());
    EXPECT_EQ(num_volumes, bpy::len(l));

    for (uint32_t i = 0; i < num_volumes; ++i)
    {
        const auto& obj(l[i]);
        const std::string v = bpy::extract<std::string>(obj);
        EXPECT_TRUE(volumes.find(v) != volumes.end());
    }
}

TEST_F(PythonClientTest, snapshot_excessive_metadata)
{
    const vfs::FrontendPath vpath(make_volume_name("/some-volume"));
    const std::string vname(create_file(vpath, 10 << 20));
    const std::string snapname("snapshot");

    std::vector<char> metav(vd::SnapshotPersistor::max_snapshot_metadata_size + 1,
                            'z');
    const std::string meta(metav.begin(), metav.end());

    EXPECT_THROW(client_.create_snapshot(vname, snapname, meta),
                 vfs::clienterrors::PythonClientException);

    const bpy::list l(client_.list_snapshots(vname));
    EXPECT_EQ(0, bpy::len(l));
}
TEST_F(PythonClientTest, volume_potential)
{
    uint64_t res = 0;
    ASSERT_NO_THROW(res = client_.volume_potential(local_node_id()) );
    EXPECT_LT(0, res);
    EXPECT_EQ(res,
              fs_->object_router().local_volume_potential(boost::none,
                                                          boost::none));
}


TEST_F(PythonClientTest, snapshot_metadata)
{
    const vfs::FrontendPath vpath(make_volume_name("/some-volume"));
    const std::string vname(create_file(vpath, 10 << 20));
    const std::string meta("some metadata");
    const std::string snapname("snapshot");

    client_.create_snapshot(vname, snapname, meta);
    check_snapshot(vname, snapname, meta);
}

TEST_F(PythonClientTest, snapshot_management)
{
    const vfs::FrontendPath vpath(make_volume_name("/some-volume"));
    const std::string vname(create_file(vpath, 10 << 20));

    const uint32_t snap_num = 10;
    bpy::list modeled_snapshots;

    for (uint32_t i = 0; i < snap_num; i++)
    {

        std::string snapname;
        while(true)
        {
            try
            {
                snapname = client_.create_snapshot(vname);
                break;
            }
            catch(vfs::clienterrors::PreviousSnapshotNotOnBackendException&)
            {
                usleep(10000);
            }
        }


        modeled_snapshots.append(snapname);
        check_snapshot(vname, snapname);
        EXPECT_TRUE(modeled_snapshots == client_.list_snapshots(vname));
    }

    EXPECT_THROW(client_.delete_snapshot(vname, "non-existing-snaphot"),
                 vfs::clienterrors::SnapshotNotFoundException);

    for (uint32_t cnt = 0; cnt < 3; cnt++)
    {
        std::string snap_todelete = bpy::extract<std::string>(modeled_snapshots[2]);
        client_.delete_snapshot(vname, snap_todelete);
        modeled_snapshots.remove(snap_todelete);
        EXPECT_TRUE(modeled_snapshots == client_.list_snapshots(vname));
    }
}

TEST_F(PythonClientTest, volume_queries)
{
    const vfs::FrontendPath vpath(make_volume_name("/testing_info"));
    const std::string vname(create_file(vpath, 10 << 20));

    const bpy::list l(client_.list_volumes());
    EXPECT_EQ(1, bpy::len(l));

    vfs::XMLRPCVolumeInfo vol_info;
    EXPECT_NO_THROW(vol_info = client_.info_volume(vname));

    EXPECT_EQ(vd::volumeFailoverStateToString(vd::VolumeFailoverState::OK_SYNC),
              vol_info.failover_mode);

    vfs::XMLRPCStatistics vol_statistics;
    EXPECT_NO_THROW(vol_statistics = client_.statistics_volume(vname));
}

TEST_F(PythonClientTest, performance_counters)
{
    const vfs::FrontendPath vpath(make_volume_name("/testing_info"));
    const std::string vname(create_file(vpath, 10 << 20));

    const size_t csize = api::GetClusterSize();

    auto expect_nothing([&](const vd::PerformanceCounter<uint64_t>& ctr)
    {
        EXPECT_EQ(0,
                  ctr.events());
        EXPECT_EQ(0,
                  ctr.sum());
        EXPECT_EQ(0,
                  ctr.sum_of_squares());
        EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
                  ctr.min());
        EXPECT_EQ(std::numeric_limits<uint64_t>::min(),
                  ctr.max());
    });

    {
        const vfs::XMLRPCStatistics stats(client_.statistics_volume(vname,
                                                                    false));

        expect_nothing(stats.performance_counters.write_request_size);
        expect_nothing(stats.performance_counters.read_request_size);
        expect_nothing(stats.performance_counters.sync_request_usecs);
    }

    const std::string pattern("The Good Son");

    write_to_file(vpath,
                  pattern,
                  csize,
                  0);

    {
        const vfs::XMLRPCStatistics stats(client_.statistics_volume(vname,
                                                                    true));
        EXPECT_EQ(1,
                  stats.performance_counters.write_request_size.events());
        EXPECT_EQ(csize,
                  stats.performance_counters.write_request_size.sum());
        EXPECT_EQ(csize * csize,
                  stats.performance_counters.write_request_size.sum_of_squares());
        EXPECT_GT(std::numeric_limits<uint64_t>::max(),
                  stats.performance_counters.write_request_size.min());
        EXPECT_EQ(4096,
                  stats.performance_counters.write_request_size.min());
        EXPECT_LT(std::numeric_limits<uint64_t>::min(),
                  stats.performance_counters.write_request_size.max());
        EXPECT_EQ(csize,
                  stats.performance_counters.write_request_size.max());

        expect_nothing(stats.performance_counters.read_request_size);
        expect_nothing(stats.performance_counters.sync_request_usecs);
    }

    {
        const vfs::XMLRPCStatistics stats(client_.statistics_volume(vname,
                                                                    false));

        expect_nothing(stats.performance_counters.write_request_size);
        expect_nothing(stats.performance_counters.read_request_size);
        expect_nothing(stats.performance_counters.sync_request_usecs);
    }
}

TEST_F(PythonClientTest, redirect)
{
    mount_remote();

    auto self = this;

    BOOST_SCOPE_EXIT((self))
    {
        self->umount_remote();
    }
    BOOST_SCOPE_EXIT_END;

    const uint64_t vsize = 1ULL << 20;
    const vfs::FrontendPath fname(make_volume_name("/some-volume"));
    const vfs::ObjectId id(create_file(fname, vsize));

    const vfs::XMLRPCVolumeInfo local_info(client_.info_volume(id));

    vfs::PythonClient remote_client(vrouter_cluster_id(),
                                    {{address(), remote_config().xmlrpc_port}});
    const vfs::XMLRPCVolumeInfo remote_info(remote_client.info_volume(id));

#define EQ_(x)                                  \
    EXPECT_TRUE(local_info.x == remote_info.x)

    EQ_(volume_id);
    EQ_(_namespace_);
    EQ_(parent_namespace);
    EQ_(parent_snapshot_id);
    EQ_(volume_size);
    EQ_(lba_size);
    EQ_(cluster_multiplier);
    EQ_(sco_multiplier);
    EQ_(failover_mode);
    EQ_(failover_ip);
    EQ_(failover_port);
    EQ_(halted);
    EQ_(footprint);
    EQ_(stored);
    EQ_(object_type);
    EQ_(parent_volume_id);
    EQ_(vrouter_id);

    // operator== is gone
    // EXPECT_TRUE(local_info == remote_info);

#undef EQ_

    const vfs::ObjectId inexistent_id(yt::UUID().str());
    EXPECT_THROW(client_.info_volume(inexistent_id),
                 vfs::clienterrors::ObjectNotFoundException);
    EXPECT_THROW(remote_client.info_volume(inexistent_id),
                 vfs::clienterrors::ObjectNotFoundException);
}

TEST_F(PythonClientTest, redirection_response)
{
    // This test just checks whether we get a redirection response from the
    // server, for all supported calls.
    // As we set max_retries = 0 we don't try to contact the peer
    // (which isn't running anyhow), but at least we receive a
    // MaxRedirectsExceededException

    vfs::PythonClient client(vrouter_cluster_id(),
                             {{address(), local_config().xmlrpc_port}},
                             0);
    vfs::ObjectId dummy_volume("dummy");

    auto registry(fs_->object_router().object_registry());

    bpt::ptree pt;
    std::shared_ptr<vfs::LockedArakoon>
        larakoon(new vfs::Registry(make_registry_config_(pt),
                                   RegisterComponent::F));
    vfs::OwnerTagAllocator owner_tag_allocator(vrouter_cluster_id(),
                                               larakoon);

    const vfs::ObjectRegistration reg(vd::Namespace(),
                                      dummy_volume,
                                      remote_config().vrouter_id,
                                      vfs::ObjectTreeConfig::makeBase(),
                                      owner_tag_allocator(),
                                      vfs::FailOverCacheConfigMode::Automatic);

    registry->TESTONLY_add_to_registry_(reg);

    auto exit(yt::make_scope_exit([&]
              {
                  registry->migrate(dummy_volume,
                                    remote_config().vrouter_id,
                                    local_config().vrouter_id);
                  registry->unregister(dummy_volume);
              }));

#define CHECK_REDIRECT(call)                                            \
    try                                                                 \
    {                                                                   \
        call;                                                           \
        ADD_FAILURE() << #call " No exception thrown, MaxRedirectsExceededException expected"; \
    }                                                                   \
    catch (vfs::clienterrors::MaxRedirectsExceededException& e)         \
    {                                                                   \
        EXPECT_EQ(remote_config().host, e.host);                        \
        EXPECT_EQ(remote_config().xmlrpc_port, e.port);                 \
    }                                                                   \
    CATCH_STD_ALL_EWHAT({                                               \
            ADD_FAILURE() << #call <<                                   \
                "expected MaxRedirectsExceededException, but got different exception: " << \
                EWHAT;                                                  \
        })

//redirection based on volumeID
    CHECK_REDIRECT(client.list_snapshots(dummy_volume));
    CHECK_REDIRECT(client.info_snapshot(dummy_volume,
                                        "non-existing snapshot"));
    CHECK_REDIRECT(client.info_volume(dummy_volume));
    CHECK_REDIRECT(client.statistics_volume(dummy_volume));
    CHECK_REDIRECT(client.create_snapshot(dummy_volume));
    CHECK_REDIRECT(client.rollback_volume(dummy_volume,
                                          "non-existing snapshot"));
    CHECK_REDIRECT(client.delete_snapshot(dummy_volume,
                                          "non-existing snapshot"));
    CHECK_REDIRECT(client.set_volume_as_template(dummy_volume));
    CHECK_REDIRECT(client.get_scrubbing_work(dummy_volume));
    CHECK_REDIRECT(client.set_cluster_cache_behaviour(dummy_volume,
                                                      boost::none));
    CHECK_REDIRECT(client.get_cluster_cache_behaviour(dummy_volume));
    CHECK_REDIRECT(client.set_cluster_cache_mode(dummy_volume,
                                                 boost::none));
    CHECK_REDIRECT(client.get_cluster_cache_mode(dummy_volume));
    CHECK_REDIRECT(client.set_cluster_cache_limit(dummy_volume,
                                                 boost::none));
    CHECK_REDIRECT(client.get_cluster_cache_limit(dummy_volume));

    CHECK_REDIRECT(client.get_sync_ignore(dummy_volume));
    CHECK_REDIRECT(client.set_sync_ignore(dummy_volume, 10, 300));
    CHECK_REDIRECT(client.get_sco_multiplier(dummy_volume));
    CHECK_REDIRECT(client.set_sco_multiplier(dummy_volume, 1024));
    CHECK_REDIRECT(client.get_tlog_multiplier(dummy_volume));
    CHECK_REDIRECT(client.set_tlog_multiplier(dummy_volume, 1024));
    CHECK_REDIRECT(client.get_sco_cache_max_non_disposable_factor(dummy_volume));
    CHECK_REDIRECT(client.set_sco_cache_max_non_disposable_factor(dummy_volume, 12.0F));
    CHECK_REDIRECT(client.set_manual_failover_cache_config(dummy_volume,
                                                           boost::none));
    CHECK_REDIRECT(client.set_automatic_failover_cache_config(dummy_volume));
    CHECK_REDIRECT(client.get_failover_cache_config(dummy_volume));

//redirection based on nodeID
    CHECK_REDIRECT(client.migrate("non-existing volume",
                                  remote_node_id()));
    CHECK_REDIRECT(client.update_cluster_node_configs(remote_node_id()));

#undef CHECK_REDIRECT
}

TEST_F(PythonClientTest, max_redirects)
{
    test_redirects(client_.max_redirects);
}

TEST_F(PythonClientTest, max_redirects_exceeded)
{
    test_redirects(client_.max_redirects + 1);
}

TEST_F(PythonClientTest, set_as_template)
{
    const vfs::FrontendPath vpath(make_volume_name("/test_template"));
    const std::string vname(create_file(vpath, 10 << 20));

    EXPECT_EQ(vfs::ObjectType::Volume, client_.info_volume(vname).object_type);
    EXPECT_NO_THROW(client_.set_volume_as_template(vname));
    EXPECT_EQ(vfs::ObjectType::Template, client_.info_volume(vname).object_type);

    //testing idempotency
    EXPECT_NO_THROW(client_.set_volume_as_template(vname));

    ASSERT_THROW(client_.create_snapshot(vname), vfs::clienterrors::InvalidOperationException);
    ASSERT_THROW(client_.get_scrubbing_work(vname), vfs::clienterrors::InvalidOperationException);
}

TEST_F(PythonClientTest, revision_info)
{
    std::string server_version_str;
    std::string client_version_str;
    ASSERT_NO_THROW(server_version_str = client_.server_revision());
    ASSERT_NO_THROW(client_version_str = client_.client_revision());
    EXPECT_TRUE(not server_version_str.empty());
    EXPECT_EQ(server_version_str, client_version_str);
}

TEST_F(PythonClientTest, scrubbing)
{
    const vfs::FrontendPath vpath(make_volume_name("/testing_the_scrubber"));
    const vfs::ObjectId vol_id(create_file(vpath, 10 << 20));
    const off_t off = 10 * api::GetClusterSize();
    const uint64_t size = 30 * api::GetClusterSize();


    const std::string pattern("some_write");
    const uint32_t snapshot_num = 10;

    std::vector<std::string> snapshot_names;
    for (uint32_t i = 0; i < snapshot_num; i++)
    {
        write_to_file(vpath, pattern, size, off);
        snapshot_names.push_back(client_.create_snapshot(vol_id));
        wait_for_snapshot(vol_id, snapshot_names.back());
    }

    auto scrub_workitems = client_.get_scrubbing_work(vol_id);
    ASSERT_EQ(snapshot_num, bpy::len(scrub_workitems));

    client_.delete_snapshot(vol_id, snapshot_names[2]);

    //successful scrub expected
    client_.apply_scrubbing_result(scrub_wrap(scrub_workitems[1]));

    //one snapshot deleted, one scrubbed
    ASSERT_EQ(snapshot_num - 2, bpy::len(client_.get_scrubbing_work(vol_id)));

    //deleted snapshot before scrubbing
    ASSERT_THROW(scrub_wrap(scrub_workitems[2]), std::exception);

    //deleted snapshot after scrubbing but before applyscrubbing
    {
        auto result = scrub_wrap(scrub_workitems[3]);
        client_.delete_snapshot(vol_id, snapshot_names[3]);
        ASSERT_THROW(client_.apply_scrubbing_result(result), std::exception);
    }

    uint32_t togo = bpy::len(client_.get_scrubbing_work(vol_id));
    //two snapshots deleted, one scrubbed
    ASSERT_EQ(snapshot_num - 3, togo);

    //happy-path cleanup expected
    while(true)
    {
        auto scrub_workitems = client_.get_scrubbing_work(vol_id);
        ASSERT_EQ(togo, bpy::len(scrub_workitems));
        if (togo == 0)
        {
            break;
        }
        client_.apply_scrubbing_result(scrub_wrap(scrub_workitems[0]));
        togo--;
    }
}

TEST_F(PythonClientTest, volume_creation)
{
    const vfs::FrontendPath vpath("/volume");
    const yt::DimensionedValue size("1GiB");
    const vfs::ObjectId cname(client_.create_volume(vpath.str(),
                                                    make_metadata_backend_config(),
                                                    size.toString()));

    const vfs::XMLRPCVolumeInfo info(client_.info_volume(cname.str()));
    EXPECT_EQ(vfs::ObjectType::Volume, info.object_type);
    EXPECT_EQ(local_node_id(), vfs::NodeId(info.vrouter_id));
}

TEST_F(PythonClientTest, clone_from_template)
{
    const vfs::FrontendPath tpath(make_volume_name("/template"));
    const vfs::ObjectId tname(create_file(tpath));

    const vfs::FrontendPath cpath("/clone");
    ASSERT_THROW(client_.create_clone_from_template(cpath.str(),
                                                    make_metadata_backend_config(),
                                                    tname.str()),
                 vfs::clienterrors::InvalidOperationException);

    client_.set_volume_as_template(tname.str());
    const vfs::ObjectId cname(client_.create_clone_from_template(cpath.str(),
                                                                 make_metadata_backend_config(),
                                                                 tname.str()));

    const vfs::XMLRPCVolumeInfo info(client_.info_volume(cname.str()));
    EXPECT_EQ(vfs::ObjectType::Volume, info.object_type);
    EXPECT_EQ(tname, vfs::ObjectId(info.parent_volume_id));
    EXPECT_EQ(local_node_id(), vfs::NodeId(info.vrouter_id));
}

TEST_F(PythonClientTest, clone)
{
    const vfs::FrontendPath ppath(make_volume_name("/parent"));
    const vfs::ObjectId pname(create_file(ppath));

    const vd::SnapshotName snap("snapshot");
    EXPECT_EQ(snap, client_.create_snapshot(pname,
                                            snap));
    wait_for_snapshot(pname,
                      snap);

    const vfs::FrontendPath cpath("/clone");
    const vfs::ObjectId cname(client_.create_clone(cpath.str(),
                                                   make_metadata_backend_config(),
                                                   pname,
                                                   snap));

    const vfs::XMLRPCVolumeInfo info(client_.info_volume(cname.str()));
    EXPECT_EQ(vfs::ObjectType::Volume, info.object_type);
    EXPECT_EQ(pname, vfs::ObjectId(info.parent_volume_id));
    EXPECT_EQ(local_node_id(), vfs::NodeId(info.vrouter_id));
}

TEST_F(PythonClientTest, prevent_orphaned_clones)
{
    const vfs::FrontendPath ppath(make_volume_name("/parent"));
    const vfs::ObjectId pname(create_file(ppath));

    const vd::SnapshotName snap("snapshot");
    EXPECT_EQ(snap, client_.create_snapshot(pname,
                                            snap));
    wait_for_snapshot(pname,
                      snap);

    auto check_snap([&]
                    {
                        std::list<std::string> l;

                        LOCKVD();
                        api::showSnapshots(static_cast<const vd::VolumeId>(pname),
                                           l);

                        ASSERT_EQ(1, l.size());
                        ASSERT_EQ(snap, l.front());
                    });

    check_snap();

    const vfs::FrontendPath cpath1("/clone-1");
    const vfs::ObjectId cname1(client_.create_clone(cpath1.str(),
                                                    make_metadata_backend_config(),
                                                    pname,
                                                    snap));

    const vfs::FrontendPath cpath2("/clone-2");
    const vfs::ObjectId cname2(client_.create_clone(cpath2.str(),
                                                    make_metadata_backend_config(),
                                                    pname,
                                                    snap));

    EXPECT_THROW(client_.delete_snapshot(pname,
                                         snap),
                 vfs::clienterrors::ObjectStillHasChildrenException);
    check_snap();
    EXPECT_NE(0, unlink(ppath));
    check_snap();

    EXPECT_EQ(0, unlink(clone_path_to_volume_path(cpath1)));
    EXPECT_EQ(0, unlink(cpath1));

    EXPECT_THROW(client_.delete_snapshot(pname,
                                         snap),
                 vfs::clienterrors::ObjectStillHasChildrenException);
    check_snap();
    EXPECT_NE(0, unlink(ppath));
    check_snap();

    EXPECT_EQ(0, unlink(clone_path_to_volume_path(cpath2)));
    EXPECT_EQ(0, unlink(cpath2));

    EXPECT_NO_THROW(client_.delete_snapshot(pname,
                                            snap));

    EXPECT_EQ(0, unlink(ppath));

    LOCKVD();
    ASSERT_THROW(api::getVolumePointer(static_cast<const vd::VolumeId>(pname)),
                 std::exception);
}

TEST_F(PythonClientTest, prevent_rollback_beyond_clone)
{
    const vfs::FrontendPath ppath(make_volume_name("/parent"));
    const vfs::ObjectId pname(create_file(ppath, 1ULL << 20));

    std::vector<vd::SnapshotName> snaps;
    snaps.reserve(4);

    const off_t off = api::GetClusterSize();
    const uint64_t size = api::GetClusterSize();

    for (size_t i = 0; i < snaps.capacity(); ++i)
    {
        const std::string snap("iteration" + boost::lexical_cast<std::string>(i));
        write_to_file(ppath, snap, size, off);
        client_.create_snapshot(pname, snap);
        snaps.emplace_back(snap);
        wait_for_snapshot(pname, snaps.back());
    }

    auto check_snaps([&](size_t snaps_left)
                     {
                         std::list<std::string> l;

                         {
                             LOCKVD();
                             api::showSnapshots(static_cast<const vd::VolumeId>(pname),
                                                l);
                         }

                         ASSERT_EQ(snaps_left, l.size());

                         auto it = l.begin();
                         for (size_t i = 0; i < snaps_left; ++i)
                         {
                             ASSERT_TRUE(it != l.end());
                             ASSERT_EQ(snaps.at(i), *it);
                             ++it;
                         }
                     });

    check_snaps(snaps.size());

    const vfs::FrontendPath cpath("/clone");
    const vfs::ObjectId cname(client_.create_clone(cpath.str(),
                                                   make_metadata_backend_config(),
                                                   pname,
                                                   snaps.at(1)));

    check_file(clone_path_to_volume_path(cpath),
               snaps.at(1),
               size,
               off);

    EXPECT_NO_THROW(client_.rollback_volume(pname,
                                            snaps.at(2)));
    check_snaps(3);

    EXPECT_NO_THROW(client_.rollback_volume(pname,
                                            snaps.at(1)));
    check_snaps(2);

    EXPECT_THROW(client_.rollback_volume(pname,
                                         snaps.at(0)),
                 vfs::clienterrors::ObjectStillHasChildrenException);

    check_snaps(2);

    check_file(clone_path_to_volume_path(cpath),
               snaps.at(1),
               size,
               off);

    EXPECT_EQ(0, unlink(clone_path_to_volume_path(cpath)));
    EXPECT_EQ(0, unlink(cpath));

    EXPECT_NO_THROW(client_.rollback_volume(pname,
                                            snaps.at(0)));

    check_snaps(1);
}

TEST_F(PythonClientTest, no_scrub_work_from_parents)
{
    const vfs::FrontendPath ppath(make_volume_name("/parent"));
    const vfs::ObjectId pname(create_file(ppath));

    const vd::SnapshotName snap("snapshot");
    EXPECT_EQ(snap, client_.create_snapshot(pname,
                                            snap));
    wait_for_snapshot(pname,
                      snap);

    const vfs::FrontendPath cpath("/clone");
    const vfs::ObjectId cname(client_.create_clone(cpath.str(),
                                                   make_metadata_backend_config(),
                                                   pname,
                                                   snap));

    EXPECT_THROW(client_.get_scrubbing_work(pname),
                 vfs::clienterrors::ObjectStillHasChildrenException);

    EXPECT_EQ(0, unlink(clone_path_to_volume_path(cpath)));
    EXPECT_EQ(0, unlink(cpath));

    EXPECT_NO_THROW(client_.get_scrubbing_work(pname));
}

TEST_F(PythonClientTest, no_scrub_application_to_parents)
{
    const vfs::FrontendPath ppath(make_volume_name("/parent"));
    const vfs::ObjectId pname(create_file(ppath, 10ULL << 20));

    const off_t off = 1 * api::GetClusterSize();
    const uint64_t size = 9 * api::GetClusterSize();


    const std::string pattern("Have you ever heard about the Higgs Boson Blues?");
    const uint32_t snapshot_num = 3;

    std::vector<std::string> snapshot_names;
    snapshot_names.reserve(snapshot_num);

    for (uint32_t i = 0; i < snapshot_num; i++)
    {
        write_to_file(ppath, pattern, size, off);
        snapshot_names.emplace_back(client_.create_snapshot(pname));
        wait_for_snapshot(pname, snapshot_names.back());
    }

    const vd::SnapshotName snap("snapshot");
    EXPECT_EQ(snap, client_.create_snapshot(pname,
                                            snap));

    wait_for_snapshot(pname,
                      snap);

    client_.delete_snapshot(pname, snapshot_names[1]);

    auto work(client_.get_scrubbing_work(pname));

    const vfs::FrontendPath cpath("/clone");
    const vfs::ObjectId cname(client_.create_clone(cpath.str(),
                                                   make_metadata_backend_config(),
                                                   pname,
                                                   snap));

    auto result(scrub_wrap(work[0]));

    EXPECT_THROW(client_.apply_scrubbing_result(result),
                 vfs::clienterrors::ObjectStillHasChildrenException);

    EXPECT_EQ(0, unlink(clone_path_to_volume_path(cpath)));
    EXPECT_EQ(0, unlink(cpath));

    EXPECT_NO_THROW(client_.apply_scrubbing_result(result));
}

TEST_F(PythonClientTest, no_templating_of_files)
{
    const vfs::FrontendPath fpath("/file");
    const vfs::ObjectId fname(create_file(fpath));
    EXPECT_THROW(client_.set_volume_as_template(fname),
                 vfs::clienterrors::InvalidOperationException);
}

TEST_F(PythonClientTest, no_clone_from_file)
{
    const vfs::FrontendPath fpath("/file");
    const vfs::ObjectId fname(create_file(fpath));

    const vfs::FrontendPath cpath("/clone");
    EXPECT_THROW(client_.create_clone_from_template(cpath.str(),
                                                    make_metadata_backend_config(),
                                                    fname),
                 vfs::clienterrors::InvalidOperationException);

    const vd::SnapshotName snap("snap");
    EXPECT_THROW(client_.create_clone(cpath.str(),
                                      make_metadata_backend_config(),
                                      fname,
                                      snap),
                 vfs::clienterrors::InvalidOperationException);
}

TEST_F(PythonClientTest, no_clone_from_template_with_snapshot)
{
    const vfs::FrontendPath ppath(make_volume_name("/parent"));
    const vfs::ObjectId pname(create_file(ppath));

    const vd::SnapshotName snap("snapshot");
    EXPECT_EQ(snap, client_.create_snapshot(pname,
                                            snap));
    wait_for_snapshot(pname,
                      snap);

    client_.set_volume_as_template(pname.str());

    const vfs::FrontendPath cpath("/clone");

    EXPECT_THROW(client_.create_clone(cpath.str(),
                                      make_metadata_backend_config(),
                                      pname,
                                      snap),
                 vfs::clienterrors::InvalidOperationException);
}

TEST_F(PythonClientTest, remote_clone)
{
    mount_remote();

    auto on_exit(yt::make_scope_exit([&]
                                     {
                                         umount_remote();
                                     }));

    const vfs::FrontendPath tpath(make_volume_name("/template"));
    const vfs::ObjectId tname(create_file(tpath));

    client_.set_volume_as_template(tname.str());

    const vfs::FrontendPath cpath("/clone");
    const vfs::ObjectId cname(client_.create_clone_from_template(cpath.str(),
                                                                 make_metadata_backend_config(),
                                                                 tname.str(),
                                                                 remote_node_id()));

    const vfs::XMLRPCVolumeInfo info(client_.info_volume(cname.str()));
    EXPECT_EQ(vfs::ObjectType::Volume, info.object_type);
    EXPECT_EQ(tname, vfs::ObjectId(info.parent_volume_id));
    EXPECT_EQ(remote_node_id(), vfs::NodeId(info.vrouter_id));
}

TEST_F(PythonClientTest, get_volume_id)
{
    {
        const vfs::FrontendPath vpath(make_volume_name("/some-volume"s));
        const vfs::ObjectId vname(create_file(vpath, 10 << 20));

        const boost::optional<vd::VolumeId> maybe_id(client_.get_volume_id(vpath.str()));
        ASSERT_TRUE(static_cast<bool>(maybe_id));
        EXPECT_EQ(vname, *maybe_id);
    }

    {
        const vfs::FrontendPath fpath("/some-file");
        create_file(fpath);

        const boost::optional<vd::VolumeId> maybe_id(client_.get_volume_id(fpath.str()));
        ASSERT_FALSE(maybe_id);
    }

    {
        const vfs::FrontendPath nopath("/does-not-exist");
        verify_absence(nopath);

        const boost::optional<vd::VolumeId> maybe_id(client_.get_volume_id(nopath.str()));
        ASSERT_FALSE(maybe_id);
    }
}

TEST_F(PythonClientTest, mds_management)
{
    const mds::ServerConfig scfg1(mds_test_setup_->next_server_config());
    mds_manager_->start_one(scfg1);

    const mds::ServerConfig scfg2(mds_test_setup_->next_server_config());
    mds_manager_->start_one(scfg2);

    ASSERT_NE(scfg1,
              scfg2);

    const vfs::FrontendPath vpath("/volume");
    const yt::DimensionedValue size("1GiB");

    const vd::MDSNodeConfigs ncfgs{ scfg1.node_config,
                                    scfg2.node_config };

    boost::shared_ptr<vd::MDSMetaDataBackendConfig>
        mcfg(new vd::MDSMetaDataBackendConfig(ncfgs,
                                              vd::ApplyRelocationsToSlaves::T));

    const vfs::ObjectId vname(client_.create_volume(vpath.str(),
                                                    mcfg,
                                                    size.toString()));

    auto check([&](const vd::MDSNodeConfigs& ref)
               {
                   const vfs::XMLRPCVolumeInfo
                       info(client_.info_volume(vname.str()));

                   auto c(boost::dynamic_pointer_cast<const vd::MDSMetaDataBackendConfig>(info.metadata_backend_config));

                   ASSERT_TRUE(ref == c->node_configs());
               });

    check(ncfgs);

    client_.update_metadata_backend_config(vname.str(),
                                           mcfg);

    check(ncfgs);

    const vd::MDSNodeConfigs ncfgs2{ scfg2.node_config,
                                     scfg1.node_config };

    boost::shared_ptr<vd::MDSMetaDataBackendConfig>
        mcfg2(new vd::MDSMetaDataBackendConfig(ncfgs2,
                                               vd::ApplyRelocationsToSlaves::T));

    client_.update_metadata_backend_config(vname.str(),
                                           mcfg2);

    check(ncfgs2);
}

TEST_F(PythonClientTest, sync_ignore)
{
    const vfs::FrontendPath vpath(make_volume_name("/syncignore-test"));
    const std::string vname(create_file(vpath, 10 << 20));
    auto res = client_.get_sync_ignore(vname);

    uint64_t number_of_syncs_to_ignore = bpy::extract<uint64_t>(res[vfs::XMLRPCKeys::number_of_syncs_to_ignore]);

    uint64_t maximum_time_to_ignore_syncs_in_seconds = bpy::extract<uint64_t>(res[vfs::XMLRPCKeys::maximum_time_to_ignore_syncs_in_seconds]);

    EXPECT_EQ(0, number_of_syncs_to_ignore);
    EXPECT_EQ(0, maximum_time_to_ignore_syncs_in_seconds);

    const uint64_t number_of_syncs_to_ignore_c = 234;
    const uint64_t maximum_time_to_ignore_syncs_in_seconds_c = 3234;

    client_.set_sync_ignore(vname,
                            number_of_syncs_to_ignore_c,
                            maximum_time_to_ignore_syncs_in_seconds_c);

    res = client_.get_sync_ignore(vname);

    number_of_syncs_to_ignore = bpy::extract<uint64_t>(res[vfs::XMLRPCKeys::number_of_syncs_to_ignore]);
    maximum_time_to_ignore_syncs_in_seconds = bpy::extract<uint64_t>(res[vfs::XMLRPCKeys::maximum_time_to_ignore_syncs_in_seconds]);

    EXPECT_EQ(number_of_syncs_to_ignore_c, number_of_syncs_to_ignore);
    EXPECT_EQ(maximum_time_to_ignore_syncs_in_seconds_c, maximum_time_to_ignore_syncs_in_seconds);

    client_.set_sync_ignore(vname,
                            0,
                            0);

    res = client_.get_sync_ignore(vname);
    number_of_syncs_to_ignore = bpy::extract<uint64_t>(res[vfs::XMLRPCKeys::number_of_syncs_to_ignore]);
    maximum_time_to_ignore_syncs_in_seconds = bpy::extract<uint64_t>(res[vfs::XMLRPCKeys::maximum_time_to_ignore_syncs_in_seconds]);

    EXPECT_EQ(0, number_of_syncs_to_ignore);
    EXPECT_EQ(0, maximum_time_to_ignore_syncs_in_seconds);
}

TEST_F(PythonClientTest, sco_multiplier)
{
    const vfs::FrontendPath vpath(make_volume_name("/sco_multiplier-test"));
    const std::string vname(create_file(vpath, 10 << 20));

    uint32_t sco_multiplier = client_.get_sco_multiplier(vname);
    EXPECT_EQ(1024, sco_multiplier);

    const uint32_t sco_multiplier_c = sco_multiplier + 1;
    client_.set_sco_multiplier(vname, sco_multiplier_c);
    sco_multiplier = client_.get_sco_multiplier(vname);
    EXPECT_EQ(sco_multiplier_c, sco_multiplier);

    EXPECT_THROW(client_.set_sco_multiplier(vname, 1),
                 vfs::clienterrors::InvalidOperationException);

    // could throw an InvalidOperationException or an InsufficientResourcesException
    // depending on the available SCOCache space
    EXPECT_THROW(client_.set_sco_multiplier(vname, 32769),
                 std::exception);
}

TEST_F(PythonClientTest, tlog_multiplier)
{
    const vfs::FrontendPath vpath(make_volume_name("/tlog_multiplier-test"));
    const std::string vname(create_file(vpath, 10 << 20));

    EXPECT_EQ(boost::none,
              client_.get_tlog_multiplier(vname));

    const uint32_t tlog_multiplier_c = 1024;
    client_.set_tlog_multiplier(vname, tlog_multiplier_c);
    const boost::optional<uint32_t>
        tlog_multiplier(client_.get_tlog_multiplier(vname));

    ASSERT_NE(boost::none,
              tlog_multiplier);

    EXPECT_EQ(1024,
              *tlog_multiplier);

    client_.set_tlog_multiplier(vname,
                                boost::none);
    EXPECT_EQ(boost::none,
              client_.get_tlog_multiplier(vname));
}

TEST_F(PythonClientTest, max_non_disposable_factor)
{
    const vfs::FrontendPath vpath(make_volume_name("/max_non_disposable_factor-test"));
    const std::string vname(create_file(vpath, 10 << 20));

    EXPECT_EQ(boost::none,
              client_.get_sco_cache_max_non_disposable_factor(vname));

    const float max_non_disposable_factor_c = 12.0F;
    client_.set_sco_cache_max_non_disposable_factor(vname,
                                                    max_non_disposable_factor_c);

    const boost::optional<float>
        max_non_disposable_factor(client_.get_sco_cache_max_non_disposable_factor(vname));
    ASSERT_NE(boost::none,
              max_non_disposable_factor);

    EXPECT_EQ(max_non_disposable_factor_c,
              *max_non_disposable_factor);

    EXPECT_THROW(client_.set_sco_cache_max_non_disposable_factor(vname,
                                                                 0.99F),
                 vfs::clienterrors::InvalidOperationException);

    client_.set_sco_cache_max_non_disposable_factor(vname,
                                                    boost::none);

    EXPECT_EQ(boost::none,
              client_.get_sco_cache_max_non_disposable_factor(vname));
}

TEST_F(PythonClientTest, cluster_cache_behaviour)
{
    const vfs::FrontendPath vpath(make_volume_name("/cluster-cache-behaviour-test"));
    const std::string vname(create_file(vpath, 10 << 20));

    ASSERT_EQ(boost::none,
              client_.get_cluster_cache_behaviour(vname));

    auto fun([&](const boost::optional<vd::ClusterCacheBehaviour>& b)
             {
                 client_.set_cluster_cache_behaviour(vname,
                                                     b);

                 ASSERT_EQ(b,
                           client_.get_cluster_cache_behaviour(vname));
             });

    fun(vd::ClusterCacheBehaviour::CacheOnRead);
    fun(vd::ClusterCacheBehaviour::CacheOnWrite);
    fun(vd::ClusterCacheBehaviour::NoCache);
    fun(boost::none);
}

TEST_F(PythonClientTest, cluster_cache_mode)
{
    const vfs::FrontendPath vpath(make_volume_name("/cluster-cache-mode-test"));
    const std::string vname(create_file(vpath, 10 << 20));

    ASSERT_EQ(boost::none,
              client_.get_cluster_cache_mode(vname));

    ASSERT_EQ(vd::ClusterCacheMode::ContentBased,
              vd::VolManager::get()->get_cluster_cache_default_mode());

    auto fun([&](const boost::optional<vd::ClusterCacheMode>& m)
             {
                 client_.set_cluster_cache_mode(vname,
                                                m);

                 ASSERT_EQ(m,
                           client_.get_cluster_cache_mode(vname));
             });

    fun(vd::ClusterCacheMode::ContentBased);
    fun(boost::none);
    fun(vd::ClusterCacheMode::LocationBased);

    EXPECT_THROW(fun(vd::ClusterCacheMode::ContentBased),
                 std::exception);

    EXPECT_EQ(vd::ClusterCacheMode::LocationBased,
              client_.get_cluster_cache_mode(vname));
}

TEST_F(PythonClientTest, cluster_cache_limit)
{
    const vfs::FrontendPath vpath(make_volume_name("/cluster-cache-mode-test"));
    const std::string vname(create_file(vpath, 10 << 20));

    ASSERT_EQ(boost::none,
              client_.get_cluster_cache_limit(vname));

    auto fun([&](const boost::optional<vd::ClusterCount>& l)
             {
                 client_.set_cluster_cache_limit(vname,
                                                 l);

                 const boost::optional<vd::ClusterCount>
                     m(client_.get_cluster_cache_limit(vname));
                 ASSERT_EQ(l,
                           m);
             });

    fun(vd::ClusterCount(1));

    EXPECT_THROW(fun(vd::ClusterCount(0)),
                 std::exception);

    EXPECT_EQ(vd::ClusterCount(1),
              *client_.get_cluster_cache_limit(vname));

    fun(boost::none);
}

TEST_F(PythonClientTest, update_cluster_node_configs)
{
    ASSERT_NO_THROW(client_.update_cluster_node_configs(std::string()));

    vfs::ObjectRouter& router = fs_->object_router();

    ASSERT_EQ(local_config(),
              router.node_config());
    ASSERT_EQ(remote_config(),
              *router.node_config(remote_config().vrouter_id));

    std::shared_ptr<vfs::ClusterRegistry> registry(router.cluster_registry());
    const vfs::ClusterNodeConfigs configs(registry->get_node_configs());

    registry->erase_node_configs();
    registry->set_node_configs({ router.node_config() });

    ASSERT_NO_THROW(client_.update_cluster_node_configs(std::string()));

    ASSERT_EQ(local_config(),
              router.node_config());
    ASSERT_EQ(boost::none,
              router.node_config(remote_config().vrouter_id));

    registry->erase_node_configs();
    registry->set_node_configs({ configs });

    ASSERT_NO_THROW(client_.update_cluster_node_configs(local_config().vrouter_id));

    ASSERT_EQ(local_config(),
              router.node_config());
    ASSERT_EQ(remote_config(),
              *router.node_config(remote_config().vrouter_id));
}

TEST_F(PythonClientTest, vaai_copy)
{
    {
        const bpy::list l(client_.list_volumes());
        EXPECT_EQ(0, bpy::len(l));
    }

    const vfs::FrontendPath vpath(make_volume_name("/vol"));
    const vfs::ObjectId vname(create_file(vpath, 10 << 20));

    const vfs::FrontendPath fc_vpath(make_volume_name("/vol-full-clone"));
    const vfs::ObjectId fc_vname(create_file(fc_vpath, 10 << 20));
    {
        const bpy::list l(client_.list_volumes());
        EXPECT_EQ(2, bpy::len(l));
    }

    const std::string src_path("/vol-flat.vmdk");
    const std::string fc_target_path("/vol-full-clone-flat.vmdk");
    const uint64_t timeout = 10;
    const vfs::CloneFileFlags fc_flags(vfs::CloneFileFlags::SkipZeroes);

    EXPECT_NO_THROW(client_.vaai_copy(src_path,
                                      fc_target_path,
                                      timeout,
                                      fc_flags));

    const vfs::CloneFileFlags lz_flags(vfs::CloneFileFlags::Lazy |
                                       vfs::CloneFileFlags::Guarded);

    const std::string lz_target_path("/vol-lazy-snapshot-flat.vmdk");
    EXPECT_NO_THROW(client_.vaai_copy(src_path,
                                      lz_target_path,
                                      timeout,
                                      lz_flags));

    const std::string nonexistent_target_path("/non-existent-volume-flat.vmdk");
    EXPECT_THROW(client_.vaai_copy(src_path,
                                   nonexistent_target_path,
                                   timeout,
                                   fc_flags),
                                   vfs::clienterrors::ObjectNotFoundException);

    EXPECT_THROW(client_.vaai_copy(src_path,
                                   lz_target_path,
                                   timeout,
                                   lz_flags),
                                   vfs::clienterrors::FileExistsException);

    const vfs::FrontendPath fc_size_mismatch(make_volume_name("/vol-size-mismatch"));
    const vfs::ObjectId sm_vname(create_file(fc_size_mismatch, 10 << 10));

    EXPECT_THROW(client_.vaai_copy(fc_size_mismatch.string(),
                                   fc_target_path,
                                   timeout,
                                   fc_flags),
                                   vfs::clienterrors::InvalidOperationException);

    const std::string nonexistent_src_path("/non-existent-src-volume-flat.vmdk");
    EXPECT_THROW(client_.vaai_copy(nonexistent_src_path,
                                   fc_target_path,
                                   timeout,
                                   fc_flags),
                                   vfs::clienterrors::ObjectNotFoundException);

    EXPECT_THROW(client_.vaai_copy(nonexistent_src_path,
                                   lz_target_path,
                                   timeout,
                                   lz_flags),
                                   vfs::clienterrors::ObjectNotFoundException);

    const vfs::CloneFileFlags fault_flags(static_cast<vfs::CloneFileFlags>(0));
    const vfs::FrontendPath fc_vpath_2(make_volume_name("/vol-full-clone-2"));
    const vfs::ObjectId fc_vname_2(create_file(fc_vpath_2, 10 << 20));
    const std::string fc_target_path_2("/vol-full-clone-2-flat.vmdk");
    EXPECT_THROW(client_.vaai_copy(src_path,
                                   fc_target_path_2,
                                   timeout,
                                   fault_flags),
                                   vfs::clienterrors::InvalidOperationException);

    const std::string src_vmdk_path("/vol.vmdk");
    const vfs::ObjectId fid(create_file(vfs::FrontendPath(src_vmdk_path),
                                        4096));
    const std::string non_existent_vmdk_path("/vol-non-existent.vmdk");
    EXPECT_NO_THROW(client_.vaai_copy(src_vmdk_path,
                                      non_existent_vmdk_path,
                                      timeout,
                                      lz_flags));
    /* Non-existent path now exists - call again the filecopy */
    EXPECT_NO_THROW(client_.vaai_copy(src_vmdk_path,
                                      non_existent_vmdk_path,
                                      timeout,
                                      lz_flags));

    EXPECT_THROW(client_.vaai_copy(src_vmdk_path,
                                   non_existent_vmdk_path,
                                   timeout,
                                   fault_flags),
                                   vfs::clienterrors::InvalidOperationException);
}

TEST_F(PythonClientTest, failovercache_config)
{
    start_failovercache_for_remote_node();

    const vfs::FrontendPath vpath(make_volume_name("/failovercacheconfig-test"));
    const std::string vname(create_file(vpath, 10 << 20));

    const vd::FailOverCacheConfig cfg1(check_initial_foc_config(vname));

    check_foc_state(vname,
                    vd::VolumeFailoverState::OK_SYNC);

    client_.set_manual_failover_cache_config(vname,
                                             boost::none);
    check_foc_config(vname,
                     vfs::FailOverCacheConfigMode::Manual,
                     boost::none);

    check_foc_state(vname,
                    vd::VolumeFailoverState::OK_STANDALONE);

    const vd::FailOverCacheConfig cfg2(local_config().host,
                                       local_config().failovercache_port);

    ASSERT_NE(cfg1,
              cfg2);

    client_.set_manual_failover_cache_config(vname,
                                             cfg2);

    check_foc_config(vname,
                     vfs::FailOverCacheConfigMode::Manual,
                     cfg2);

    check_foc_state(vname,
                    vd::VolumeFailoverState::OK_SYNC);

    const vd::FailOverCacheConfig cfg3("somewhereoutthere"s,
                                       local_config().failovercache_port);

    client_.set_manual_failover_cache_config(vname,
                                             cfg3);

    check_foc_config(vname,
                     vfs::FailOverCacheConfigMode::Manual,
                     cfg3);

    check_foc_state(vname,
                    vd::VolumeFailoverState::DEGRADED);

    client_.set_automatic_failover_cache_config(vname);

    check_foc_config(vname,
                     vfs::FailOverCacheConfigMode::Automatic,
                     cfg1);

    check_foc_state(vname,
                    vd::VolumeFailoverState::OK_SYNC);
}


}