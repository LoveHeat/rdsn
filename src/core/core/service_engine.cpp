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

/*
 * Description:
 *     What is this file about?
 *
 * Revision history:
 *     xxxx-xx-xx, author, first version
 *     xxxx-xx-xx, author, fix bug about xxx
 */

#include "task_engine.h"
#include "disk_engine.h"

#include <dsn/tool-api/rpc_engine.h>
#include <dsn/tool-api/service_engine.h>
#include <dsn/tool-api/uri_address.h>
#include <dsn/tool-api/env_provider.h>
#include <dsn/utility/factory_store.h>
#include <dsn/utility/filesystem.h>
#include <dsn/tool-api/command_manager.h>
#include <dsn/tool-api/perf_counter.h>
#include <dsn/tool-api/perf_counters.h>
#include <dsn/tool_api.h>
#include <dsn/tool/node_scoper.h>

#ifdef __TITLE__
#undef __TITLE__
#endif
#define __TITLE__ "service_engine"

using namespace dsn::utils;

namespace dsn {

service_node::service_node(service_app_spec &app_spec)
{
    _computation = nullptr;
    _app_spec = app_spec;
}

bool service_node::rpc_register_handler(task_code code,
                                        const char *extra_name,
                                        const rpc_request_handler &h)
{
    return _node_io.rpc->register_rpc_handler(code, extra_name, h);
}

bool service_node::rpc_unregister_handler(dsn::task_code rpc_code)
{
    return _node_io.rpc->unregister_rpc_handler(rpc_code);
}

error_code service_node::init_io_engine()
{
    auto &spec = service_engine::fast_instance().spec();
    error_code err = ERR_OK;

    // init timer service
    _node_io.tsvc = factory_store<timer_service>::create(
        service_engine::fast_instance().spec().timer_factory_name.c_str(),
        PROVIDER_TYPE_MAIN,
        this,
        nullptr);
    for (auto &s : service_engine::fast_instance().spec().timer_aspects) {
        _node_io.tsvc = factory_store<timer_service>::create(
            s.c_str(), PROVIDER_TYPE_ASPECT, this, _node_io.tsvc);
    }

    // init disk engine
    _node_io.disk = new disk_engine(this);
    aio_provider *aio = factory_store<aio_provider>::create(
        spec.aio_factory_name.c_str(), ::dsn::PROVIDER_TYPE_MAIN, _node_io.disk, nullptr);
    for (auto it = spec.aio_aspects.begin(); it != spec.aio_aspects.end(); it++) {
        aio = factory_store<aio_provider>::create(
            it->c_str(), PROVIDER_TYPE_ASPECT, _node_io.disk, aio);
    }
    _node_io.aio = aio;

    // init rpc engine
    _node_io.rpc = new rpc_engine(this);

    return err;
}

error_code service_node::start_io_engine_in_main()
{
    auto &spec = service_engine::fast_instance().spec();
    error_code err = ERR_OK;

    // start timer service
    _node_io.tsvc->start();

    // start disk engine
    _node_io.disk->start(_node_io.aio);

    // start rpc engine
    err = _node_io.rpc->start(_app_spec);
    return err;
}

dsn::error_code service_node::start_app()
{
    dassert(_entity.get(), "entity hasn't initialized");
    _entity->set_address(rpc()->primary_address());

    std::vector<std::string> args;
    utils::split_args(spec().arguments.c_str(), args);
    args.insert(args.begin(), spec().full_name);
    dsn::error_code res = _entity->start(args);
    if (res == dsn::ERR_OK) {
        _entity->set_started(true);
    }
    return res;
}

dsn::error_code service_node::stop_app(bool cleanup)
{
    dassert(_entity.get(), "entity hasn't initialized");
    dsn::error_code res = _entity->stop(cleanup);
    if (res == dsn::ERR_OK) {
        _entity->set_started(false);
    }
    return res;
}

void service_node::init_service_app()
{
    _info.entity_id = _app_spec.id;
    _info.index = _app_spec.index;
    _info.role_name = _app_spec.role_name;
    _info.type = _app_spec.type;
    _info.full_name = _app_spec.full_name;
    _info.data_dir = _app_spec.data_dir;

    _entity.reset(service_app::new_service_app(_app_spec.type, &_info));
}

error_code service_node::start()
{
    error_code err = ERR_OK;

    // init data dir
    if (!dsn::utils::filesystem::path_exists(spec().data_dir))
        dsn::utils::filesystem::create_directory(spec().data_dir);

    // init task engine
    _computation = new task_engine(this);
    _computation->create(_app_spec.pools);
    dassert(!_computation->is_started(), "task engine must not be started at this point");

    // init per node io engines
    err = init_io_engine();
    if (err != ERR_OK)
        return err;

    // start io engines (only timer, disk and rpc), others are started in app start task
    start_io_engine_in_main();

    // start task engine
    _computation->start();
    dassert(_computation->is_started(), "task engine must be started at this point");

    // create service_app
    {
        ::dsn::tools::node_scoper scoper(this);
        init_service_app();
    }

    // start rpc serving
    _node_io.rpc->start_serving();

    return err;
}

void service_node::get_runtime_info(const std::string &indent,
                                    const std::vector<std::string> &args,
                                    /*out*/ std::stringstream &ss)
{
    ss << indent << full_name() << ":" << std::endl;

    std::string indent2 = indent + "\t";
    _computation->get_runtime_info(indent2, args, ss);
}

void service_node::get_queue_info(
    /*out*/ std::stringstream &ss)
{
    ss << "{\"app_name\":\"" << full_name() << "\",\n\"thread_pool\":[\n";
    _computation->get_queue_info(ss);
    ss << "]}";
}

rpc_request_task *service_node::generate_intercepted_request_task(message_ex *req)
{
    bool is_write = task_spec::get(req->local_rpc_code)->rpc_request_is_write_operation;
    rpc_request_task *t = new rpc_request_task(req,
                                               std::bind(&service_app::on_intercepted_request,
                                                         _entity.get(),
                                                         req->header->gpid,
                                                         is_write,
                                                         std::placeholders::_1),
                                               this);
    t->spec().on_task_create.execute(nullptr, t);
    return t;
}

//////////////////////////////////////////////////////////////////////////////////////////

service_engine::service_engine(void)
{
    _env = nullptr;
    _logging = nullptr;

    ::dsn::command_manager::instance().register_command({"engine"},
                                                        "engine - get engine internal information",
                                                        "engine [app-id]",
                                                        &service_engine::get_runtime_info);
    ::dsn::command_manager::instance().register_command(
        {"system.queue"},
        "system.queue - get queue internal information",
        "system.queue",
        &service_engine::get_queue_info);
}

void service_engine::init_before_toollets(const service_spec &spec)
{
    _spec = spec;

    // init common providers (first half)
    _logging = factory_store<logging_provider>::create(
        spec.logging_factory_name.c_str(), ::dsn::PROVIDER_TYPE_MAIN, spec.dir_log.c_str());

    perf_counters::instance().register_factory(
        factory_store<perf_counter>::get_factory<perf_counter::factory>(
            spec.perf_counter_factory_name.c_str(), ::dsn::PROVIDER_TYPE_MAIN));

    // init common for all per-node providers
    message_ex::s_local_hash =
        (uint32_t)dsn_config_get_value_uint64("core",
                                              "local_hash",
                                              0,
                                              "a same hash value from two processes indicate the "
                                              "rpc code are registered in the same order, "
                                              "and therefore the mapping between rpc code string "
                                              "and integer is the same, which we leverage "
                                              "for fast rpc handler lookup optimization");
}

void service_engine::init_after_toollets()
{
    // init common providers (second half)
    _env = factory_store<env_provider>::create(
        _spec.env_factory_name.c_str(), PROVIDER_TYPE_MAIN, nullptr);
    for (auto it = _spec.env_aspects.begin(); it != _spec.env_aspects.end(); it++) {
        _env = factory_store<env_provider>::create(it->c_str(), PROVIDER_TYPE_ASPECT, _env);
    }
    tls_dsn.env = _env;
}

service_node *service_engine::start_node(service_app_spec &app_spec)
{
    auto it = _nodes_by_app_id.find(app_spec.id);
    if (it != _nodes_by_app_id.end()) {
        return it->second;
    } else {
        for (auto p : app_spec.ports) {
            // union to existing node if any port is shared
            if (_nodes_by_app_port.find(p) != _nodes_by_app_port.end()) {
                service_node *n = _nodes_by_app_port[p];

                dassert(false,
                        "network port %d usage confliction for %s vs %s, "
                        "please reconfig",
                        p,
                        n->full_name(),
                        app_spec.full_name.c_str());
            }
        }

        auto node = new service_node(app_spec);
        error_code err = node->start();
        dassert(err == ERR_OK, "service node start failed, err = %s", err.to_string());

        _nodes_by_app_id[node->id()] = node;
        for (auto p1 : node->spec().ports) {
            _nodes_by_app_port[p1] = node;
        }

        return node;
    }
}

std::string service_engine::get_runtime_info(const std::vector<std::string> &args)
{
    std::stringstream ss;
    if (args.size() == 0) {
        ss << "" << service_engine::fast_instance()._nodes_by_app_id.size()
           << " nodes available:" << std::endl;
        for (auto &kv : service_engine::fast_instance()._nodes_by_app_id) {
            ss << "\t" << kv.second->id() << "." << kv.second->full_name() << std::endl;
        }
    } else {
        std::string indent = "";
        int id = atoi(args[0].c_str());
        auto it = service_engine::fast_instance()._nodes_by_app_id.find(id);
        if (it != service_engine::fast_instance()._nodes_by_app_id.end()) {
            auto args2 = args;
            args2.erase(args2.begin());
            it->second->get_runtime_info(indent, args2, ss);
        } else {
            ss << "cannot find node with given app id";
        }
    }
    return ss.str();
}

std::string service_engine::get_queue_info(const std::vector<std::string> &args)
{
    std::stringstream ss;
    ss << "[";
    for (auto &it : service_engine::fast_instance()._nodes_by_app_id) {
        if (it.first != service_engine::fast_instance()._nodes_by_app_id.begin()->first)
            ss << ",";
        it.second->get_queue_info(ss);
    }
    ss << "]";
    return ss.str();
}

} // end namespace
