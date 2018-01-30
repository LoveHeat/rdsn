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

#pragma once

#include <dsn/utility/synchronize.h>
#include <dsn/utility/singleton.h>
#include <dsn/tool-api/rpc_message.h>
#include <map>

namespace dsn {

typedef std::function<std::string(const std::vector<std::string> &)> command_handler;
struct command_instance
{
    dsn::rpc_address address;
    std::vector<std::string> commands;
    std::string help_short;
    std::string help_long;
    command_handler handler;
};

class command_manager : public ::dsn::utils::singleton<command_manager>
{
public:
    command_manager();

    dsn_handle_t register_command(const std::vector<std::string> &commands,
                                  const std::string &help_one_line,
                                  const std::string &help_long,
                                  command_handler handler);
    dsn_handle_t register_app_command(const std::vector<std::string> &commands,
                                      const std::string &help_one_line,
                                      const std::string &help_long,
                                      command_handler handler);
    void deregister_command(dsn_handle_t handle);

    command_instance *get_command_instance(const std::string &cmd);

private:
    ::dsn::utils::rw_lock_nr _lock;
    std::map<std::string, command_instance *> _handlers;
    std::vector<command_instance *> _commands;
};
}
