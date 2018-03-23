/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 *
 * -=- Robust Distributed System Nucleus (rDSN) -=-
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "duplication_test_base.h"

#include <dsn/utility/filesystem.h>

#include "dist/replication/lib/mutation_log_utils.h"
#include "dist/replication/lib/duplication/private_log_loader.h"

namespace dsn {
namespace apps {

// for loading PUT mutations from log file.
DEFINE_TASK_CODE_RPC(RPC_RRDB_RRDB_PUT, TASK_PRIORITY_COMMON, ::dsn::THREAD_POOL_DEFAULT);

} // namespace apps
} // namespace dsn

namespace dsn {
namespace replication {

struct mutation_duplicator_test : public duplication_test_base
{
    mutation_duplicator_test() : log_dir("./test-log")
    {
        stub = make_unique<replica_stub>();
        replica = create_replica(stub.get(), 1, 1, log_dir.c_str());
    }

    void SetUp() override
    {
        utils::filesystem::remove_path(log_dir);
        utils::filesystem::create_directory(log_dir);
    }

    void TearDown() override
    {
        //        utils::filesystem::remove_path(log_dir);
    }

    mutation_ptr create_test_mutation(int64_t decree, const std::string &data)
    {
        mutation_ptr mu(new mutation());
        mu->data.header.ballot = 1;
        mu->data.header.decree = decree;
        mu->data.header.pid = replica->get_gpid();
        mu->data.header.last_committed_decree = decree - 1;
        mu->data.header.log_offset = 0;
        mu->data.header.timestamp = decree;

        std::shared_ptr<char> s(new char[data.length()]);
        memcpy(s.get(), data.data(), data.length());
        dsn::blob b(std::move(s), data.length());

        mu->data.updates.emplace_back(mutation_update());
        mu->data.updates.back().code =
            RPC_COLD_BACKUP; // whatever code it is, but never be WRITE_EMPTY
        mu->data.updates.back().data = std::move(b);
        mu->client_requests.push_back(nullptr);

        // mutation_duplicator always loads from hard disk,
        // so it must be logged.
        mu->set_logged();

        return mu;
    }

    std::unique_ptr<mutation_duplicator> create_test_duplicator()
    {
        duplication_entry dup_ent;
        dup_ent.dupid = 1;
        dup_ent.remote_address = "remote_address";
        dup_ent.status = duplication_status::DS_START;
        return make_unique<mutation_duplicator>(dup_ent, replica.get());
    }

    void test_start_duplication(int num_entries,
                                int private_log_size_mb,
                                mock_duplication_backlog_handler::error_hook hook = nullptr)
    {
        std::vector<std::string> mutations;

        mutation_log_ptr mlog = new mutation_log_private(
            replica->dir(), private_log_size_mb, replica->get_gpid(), nullptr, 1024, 512, 50000);
        EXPECT_EQ(mlog->open(nullptr, nullptr), ERR_OK);

        {
            for (int i = 0; i < num_entries; i++) {
                std::string msg = "hello!";
                mutations.push_back(msg);
                mutation_ptr mu = create_test_mutation(2 + i, msg);
                mlog->append(mu, LPC_AIO_IMMEDIATE_CALLBACK, nullptr, nullptr, 0);
            }

            // commit the last entry
            mutation_ptr mu = create_test_mutation(2 + num_entries, "hello!");
            mlog->append(mu, LPC_AIO_IMMEDIATE_CALLBACK, nullptr, nullptr, 0);

            dsn_task_tracker_wait_all(mlog->tracker());
        }

        {
            replica->init_private_log(mlog);
            auto duplicator = create_test_duplicator();
            auto backlog_handler = dynamic_cast<mock_duplication_backlog_handler *>(
                duplicator->_backlog_handler.get());
            backlog_handler->set_error_hook(std::move(hook));

            {
                duplicator->start();

                while (backlog_handler->get_mutation_list_safe().size() < mutations.size()) {
                    sleep(1);
                }

                duplicator->pause();
                duplicator->wait_all();
            }

            ASSERT_EQ(backlog_handler->mutation_list, mutations)
                << backlog_handler->mutation_list.size() << " vs " << mutations.size();
        }
    }

    void test_handle_real_private_log()
    {
        constexpr int total_writes_size = 4;
        constexpr int total_mutations_size = 7;

        ASSERT_TRUE(utils::filesystem::rename_path("log.1.0", "test-log/log.1.0"));

        mutation_log_ptr mlog = new mutation_log_private(
            replica->dir(), 4, replica->get_gpid(), nullptr, 1024, 512, 10000);
        replica->init_private_log(mlog);
        mlog->update_max_commit_on_disk(total_mutations_size); // assume all logs are committed.

        auto duplicator = create_test_duplicator();
        auto backlog_handler =
            dynamic_cast<mock_duplication_backlog_handler *>(duplicator->_backlog_handler.get());
        {
            duplicator->start();

            while (backlog_handler->get_mutation_list_safe().size() < total_writes_size) {
                sleep(1);
            }

            duplicator->pause();
            duplicator->wait_all();
        }
    }

    const std::string log_dir;

    std::unique_ptr<mock_replica> replica;
    std::unique_ptr<replica_stub> stub;
};

TEST_F(mutation_duplicator_test, new_duplicator)
{
    dupid_t dupid = 1;
    std::string remote_address = "remote_address";
    duplication_status::type status = duplication_status::DS_START;
    int64_t confirmed_decree = 100;

    duplication_entry dup_ent;
    dup_ent.dupid = dupid;
    dup_ent.remote_address = remote_address;
    dup_ent.status = status;
    dup_ent.progress[replica->get_gpid().get_partition_index()] = confirmed_decree;

    auto duplicator = make_unique<mutation_duplicator>(dup_ent, replica.get());
    ASSERT_EQ(duplicator->id(), dupid);
    ASSERT_EQ(duplicator->remote_cluster_address(), remote_address);
    ASSERT_EQ(duplicator->view().status, status);
    ASSERT_EQ(duplicator->view().confirmed_decree, confirmed_decree);
    ASSERT_EQ(duplicator->view().last_decree, confirmed_decree);
}

TEST_F(mutation_duplicator_test, find_log_file_containing_decree)
{
    std::vector<std::string> mutations;
    int max_log_file_mb = 1;

    mutation_log_ptr mlog = new mutation_log_private(
        replica->dir(), max_log_file_mb, replica->get_gpid(), nullptr, 1024, 512, 10000);
    EXPECT_EQ(mlog->open(nullptr, nullptr), ERR_OK);

    { // writing mutations to log which will generate multiple files
        for (int i = 0; i < 1000 * 50; i++) {
            std::string msg = "hello!";
            mutations.push_back(msg);
            mutation_ptr mu = create_test_mutation(2 + i, msg);
            mlog->append(mu, LPC_AIO_IMMEDIATE_CALLBACK, nullptr, nullptr, 0);
        }
    }

    auto files = log_utils::list_all_files_or_die(log_dir);

    auto lf = private_log_loader::find_log_file_containing_decree(files, replica->get_gpid(), 200);
    ASSERT_TRUE(lf);
    ASSERT_EQ(lf->index(), 1);

    lf = private_log_loader::find_log_file_containing_decree(files, replica->get_gpid(), 500);
    ASSERT_TRUE(lf);
    ASSERT_EQ(lf->index(), 1);

    std::map<int, log_file_ptr> log_file_map = log_utils::open_log_file_map(files);
    int last_idx = log_file_map.rbegin()->first;
    lf = private_log_loader::find_log_file_containing_decree(
        files, replica->get_gpid(), 1000 * 50 + 200);
    ASSERT_TRUE(lf);
    ASSERT_EQ(lf->index(), last_idx);
}

TEST_F(mutation_duplicator_test, start_duplication_10000_4MB) { test_start_duplication(10000, 4); }

TEST_F(mutation_duplicator_test, start_duplication_50000_4MB) { test_start_duplication(50000, 4); }

TEST_F(mutation_duplicator_test, start_duplication_10000_1MB) { test_start_duplication(10000, 1); }

TEST_F(mutation_duplicator_test, start_duplication_50000_1MB) { test_start_duplication(50000, 1); }

// Ensures no tasks will be running after duplicator was paused.
TEST_F(mutation_duplicator_test, pause_start_duplication)
{
    mutation_log_ptr mlog =
        new mutation_log_private(replica->dir(), 4, replica->get_gpid(), nullptr, 1024, 512, 10000);
    EXPECT_EQ(mlog->open(nullptr, nullptr), ERR_OK);

    {
        replica->init_private_log(mlog);
        auto duplicator = create_test_duplicator();
        duplicator->start();
        duplicator->pause();

        duplicator->wait_all();
    }
}

TEST_F(mutation_duplicator_test, duplication_view)
{
    auto duplicator = create_test_duplicator();
    ASSERT_EQ(duplicator->view().last_decree, 0);
    ASSERT_EQ(duplicator->view().confirmed_decree, 0);

    duplicator->update_state(duplicator->view().set_last_decree(10));
    ASSERT_EQ(duplicator->view().last_decree, 10);
    ASSERT_EQ(duplicator->view().confirmed_decree, 0);

    duplicator->update_state(duplicator->view().set_confirmed_decree(10));
    ASSERT_EQ(duplicator->view().confirmed_decree, 10);
    ASSERT_EQ(duplicator->view().last_decree, 10);
}

TEST_F(mutation_duplicator_test, fail_and_retry)
{
    test_start_duplication(1000, 1, []() -> dsn::error_s {
        static bool retry = true;
        auto err = dsn::error_s::ok();
        if (retry) {
            err = dsn::error_s::make(ERR_TIMEOUT);
        }
        retry = !retry;
        return err;
    });
}

// Ensure mutation_duplicator can correctly handle real-world log file (log.1.0).
// There are 4 puts, 3 write empties in log.1.0: PUT, PUT, PUT, EMPTY, PUT, EMPTY, EMPTY.
TEST_F(mutation_duplicator_test, real_private_log) { test_handle_real_private_log(); }

} // namespace replication
} // namespace dsn
