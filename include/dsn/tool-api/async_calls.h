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
 *     c++ client side service API
 *
 * Revision history:
 *     Sep., 2015, @imzhenyu (Zhenyu Guo), first version
 *     xxxx-xx-xx, author, fix bug about xxx
 */

#pragma once

#include <dsn/service_api_c.h>
#include <dsn/utility/function_traits.h>
#include <dsn/tool-api/task.h>
#include <dsn/tool-api/task_tracker.h>
#include <dsn/serialization/serialization.h>
#include <dsn/tool-api/rpc_engine.h>

namespace dsn {

inline void empty_rpc_handler(dsn::error_code, dsn::message_ex *, dsn::message_ex *) {}

// callback(error_code, TResponse&& response)
template <typename TFunction, class Enable = void>
struct is_typed_rpc_callback
{
    constexpr static bool const value = false;
};
template <typename TFunction>
struct is_typed_rpc_callback<TFunction,
                             typename std::enable_if<function_traits<TFunction>::arity == 2>::type>
{
    // todo: check if response_t is marshallable
    using inspect_t = function_traits<TFunction>;
    constexpr static bool const value =
        std::is_same<typename inspect_t::template arg_t<0>, dsn::error_code>::value &&
        std::is_default_constructible<
            typename std::decay<typename inspect_t::template arg_t<1>>::type>::value;
    using response_t = typename std::decay<typename inspect_t::template arg_t<1>>::type;
};

/*!
@addtogroup tasking
@{
*/

namespace tasking {
inline task_ptr
create_task(dsn::task_code code, dsn::task_tracker *tracker, task_handler &&cb, int hash = 0)
{
    dsn::task_ptr t(new dsn::raw_task(code, std::move(cb), hash, nullptr));
    t->set_tracker(tracker);
    t->spec().on_task_create.execute(::dsn::task::get_current_task(), t);
    return t;
}

inline task_ptr create_timer_task(dsn::task_code code,
                                  dsn::task_tracker *tracker,
                                  task_handler &&cb,
                                  std::chrono::milliseconds interval,
                                  int hash = 0)
{
    dsn::task_ptr t(new dsn::timer_task(
        code, std::move(cb), static_cast<uint32_t>(interval.count()), hash, nullptr));
    t->set_tracker(tracker);
    t->spec().on_task_create.execute(::dsn::task::get_current_task(), t);
    return t;
}

inline task_ptr enqueue(dsn::task_code code,
                        task_tracker *tracker,
                        task_handler &&callback,
                        int hash = 0,
                        std::chrono::milliseconds delay = std::chrono::milliseconds(0))
{
    auto tsk = create_task(code, tracker, std::move(callback), hash);
    tsk->set_delay(static_cast<int>(delay.count()));
    tsk->enqueue();
    return tsk;
}

inline task_ptr enqueue_timer(dsn::task_code evt,
                              task_tracker *tracker,
                              task_handler &&cb,
                              std::chrono::milliseconds timer_interval,
                              int hash = 0,
                              std::chrono::milliseconds delay = std::chrono::milliseconds(0))
{
    auto tsk = create_timer_task(evt, tracker, std::move(cb), timer_interval, hash);
    tsk->set_delay(static_cast<int>(delay.count()));
    tsk->enqueue();
    return tsk;
}

template <typename TCallback>
inline dsn::ref_ptr<dsn::safe_late_task<TCallback>> create_late_task(
    dsn::task_code code, const TCallback &cb, int hash = 0, task_tracker *tracker = nullptr)
{
    using result_task_type = safe_late_task<typename std::remove_cv<TCallback>::type>;
    dsn::ref_ptr<result_task_type> ptr(new result_task_type(code, std::move(cb), hash, nullptr));
    ptr->set_tracker(tracker);
    ptr->spec().on_task_create.execute(::dsn::task::get_current_task(), ptr);
    return ptr;
}
}
/*@}*/

/*!
@addtogroup rpc-client
@{
*/
namespace rpc {

inline rpc_response_task_ptr create_rpc_response_task(dsn::message_ex *req,
                                                      task_tracker *tracker,
                                                      rpc_response_handler &&callback,
                                                      int reply_thread_hash = 0)
{
    rpc_response_task_ptr t(
        new rpc_response_task((message_ex *)req, std::move(callback), reply_thread_hash, nullptr));
    t->set_tracker(tracker);
    t->spec().on_task_create.execute(::dsn::task::get_current_task(), t);
    return t;
}

template <typename TCallback>
typename std::enable_if<is_typed_rpc_callback<TCallback>::value, rpc_response_task_ptr>::type
create_rpc_response_task(dsn::message_ex *req,
                         task_tracker *tracker,
                         TCallback &&cb,
                         int reply_thread_hash = 0)
{
    return create_rpc_response_task(
        req,
        tracker,
        [cb_fwd =
             std::move(cb)](error_code err, dsn::message_ex * req, dsn::message_ex * resp) mutable {
            typename is_typed_rpc_callback<TCallback>::response_t response = {};
            if (err == dsn::ERR_OK) {
                dsn::unmarshall(resp, response);
            }
            cb_fwd(err, std::move(response));
        },
        reply_thread_hash);
}

template <typename TCallback>
rpc_response_task_ptr call(::dsn::rpc_address server,
                           dsn::message_ex *request,
                           task_tracker *tracker,
                           TCallback &&callback,
                           int reply_thread_hash = 0)
{
    rpc_response_task_ptr t = create_rpc_response_task(
        request, tracker, std::forward<TCallback>(callback), reply_thread_hash);
    task::get_current_rpc()->call(server, t);
    return t;
}

//
// for TRequest/TResponse, we assume that the following routines are defined:
//    marshall(binary_writer& writer, const T& val);
//    unmarshall(binary_reader& reader, /*out*/ T& val);
// either in the namespace of ::dsn::utils or T
// developers may write these helper functions by their own, or use tools
// such as protocol-buffer, thrift, or bond to generate these functions automatically
// for their TRequest and TResponse
//
template <typename TRequest, typename TCallback>
rpc_response_task_ptr
call(dsn::rpc_address server,
     dsn::task_code code,
     TRequest &&req,
     dsn::task_tracker *owner,
     TCallback &&callback,
     std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
     int thread_hash = 0, ///< if thread_hash == 0 && partition_hash != 0, thread_hash is
                          /// computed from partition_hash
     uint64_t partition_hash = 0,
     int reply_thread_hash = 0)
{
    dsn::message_ex *msg = dsn::message_ex::create_request(
        code, static_cast<int>(timeout.count()), thread_hash, partition_hash);
    ::dsn::marshall(msg, std::forward<TRequest>(req));
    return call(server, msg, owner, std::forward<TCallback>(callback), reply_thread_hash);
}

// no callback
template <typename TRequest>
void call_one_way_typed(::dsn::rpc_address server,
                        dsn::task_code code,
                        const TRequest &req,
                        int thread_hash = 0, ///< if thread_hash == 0 && partition_hash != 0,
                                             /// thread_hash is computed from partition_hash
                        uint64_t partition_hash = 0)
{
    dsn::message_ex *msg = dsn::message_ex::create_request(code, 0, thread_hash, partition_hash);
    ::dsn::marshall(msg, req);
    task::get_current_rpc()->call_one_way(server, msg);
}

template <typename TResponse>
std::pair<::dsn::error_code, TResponse> wait_and_unwrap(rpc_response_task_ptr tsk)
{
    tsk->wait();
    std::pair<::dsn::error_code, TResponse> result;
    result.first = tsk->error();
    if (tsk->error() == ::dsn::ERR_OK) {
        ::dsn::unmarshall(tsk->get_response(), result.second);
    }
    return result;
}

template <typename TResponse, typename TRequest>
std::pair<::dsn::error_code, TResponse>
call_wait(::dsn::rpc_address server,
          dsn::task_code code,
          TRequest &&req,
          std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
          int thread_hash = 0,
          uint64_t partition_hash = 0)
{
    return wait_and_unwrap<TResponse>(call(server,
                                           code,
                                           std::forward<TRequest>(req),
                                           nullptr,
                                           empty_rpc_handler,
                                           timeout,
                                           thread_hash,
                                           partition_hash));
}
}
/*@}*/

/*!
@addtogroup file
@{
*/
namespace file {

inline aio_task_ptr
create_aio_task(dsn::task_code code, task_tracker *tracker, aio_handler &&callback, int hash)
{
    aio_task_ptr t(new aio_task(code, std::move(callback), hash));
    t->set_tracker((dsn::task_tracker *)tracker);
    t->spec().on_task_create.execute(::dsn::task::get_current_task(), t);
    return t;
}

inline aio_task_ptr read(dsn_handle_t fh,
                         char *buffer,
                         int count,
                         uint64_t offset,
                         dsn::task_code callback_code,
                         task_tracker *tracker,
                         aio_handler &&callback,
                         int hash = 0)
{
    aio_task_ptr tsk = create_aio_task(callback_code, tracker, std::move(callback), hash);
    dsn_file_read(fh, buffer, count, offset, tsk);
    return tsk;
}

inline aio_task_ptr write(dsn_handle_t fh,
                          const char *buffer,
                          int count,
                          uint64_t offset,
                          dsn::task_code callback_code,
                          task_tracker *tracker,
                          aio_handler &&callback,
                          int hash = 0)
{
    aio_task_ptr tsk = create_aio_task(callback_code, tracker, std::move(callback), hash);
    dsn_file_write(fh, buffer, count, offset, tsk);
    return tsk;
}

inline aio_task_ptr write_vector(dsn_handle_t fh,
                                 const dsn_file_buffer_t *buffers,
                                 int buffer_count,
                                 uint64_t offset,
                                 dsn::task_code callback_code,
                                 task_tracker *tracker,
                                 aio_handler &&callback,
                                 int hash = 0)
{
    aio_task_ptr tsk = create_aio_task(callback_code, tracker, std::move(callback), hash);
    dsn_file_write_vector(fh, buffers, buffer_count, offset, tsk.get());
    return tsk;
}
}
/*@}*/
// ------------- inline implementation ----------------

} // end namespace
