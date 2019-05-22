﻿#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <common/file_system.h>
#include <common/string_oprs.h>
#include <std/explicit_declare.h>

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include <atbus_node.h>

#include "detail/libatbus_protocol.h"

#include "atbus_test_utils.h"
#include "frame/test_macros.h"

#include <stdarg.h>

static void node_reg_test_on_debug(const char *file_path, size_t line, const atbus::node &n, const atbus::endpoint *ep,
                                   const atbus::connection *conn, EXPLICIT_UNUSED_ATTR const atbus::protocol::msg *m, const char *fmt,
                                   ...) {
    size_t offset = 0;
    for (size_t i = 0; file_path[i]; ++i) {
        if ('/' == file_path[i] || '\\' == file_path[i]) {
            offset = i + 1;
        }
    }
    file_path += offset;

    std::streamsize w = std::cout.width();
    CASE_MSG_INFO() << "[Log Debug][" << std::setw(24) << file_path << ":" << std::setw(4) << line << "] node=0x" << std::setfill('0')
                    << std::hex << std::setw(8) << n.get_id() << ", ep=0x" << std::setw(8) << (NULL == ep ? 0 : ep->get_id())
                    << ", c=" << conn << std::setfill(' ') << std::setw(w) << std::dec << "\t";

    va_list ap;
    va_start(ap, fmt);

    vprintf(fmt, ap);

    va_end(ap);

    puts("");

#ifdef _MSC_VER

    static char *APPVEYOR = getenv("APPVEYOR");
    static char *CI       = getenv("CI");

    // appveyor ci open msg content
    if (APPVEYOR && APPVEYOR[0] && CI && CI[0] && NULL != m) {
        std::cout << *m << std::endl;
    }
#endif
}

static int node_reg_test_on_error(const atbus::node &n, const atbus::endpoint *ep, const atbus::connection *conn, int status, int errcode) {
    if (0 == errcode || UV_EOF == errcode) {
        return 0;
    }

    std::streamsize w = std::cout.width();
    CASE_MSG_INFO() << "[Log Error] node=0x" << std::setfill('0') << std::hex << std::setw(8) << n.get_id() << ", ep=0x" << std::setw(8)
                    << (NULL == ep ? 0 : ep->get_id()) << ", c=" << conn << std::setfill(' ') << std::setw(w) << std::dec
                    << "=> status: " << status << ", errcode: " << errcode << std::endl;
    return 0;
}

struct node_reg_test_recv_msg_record_t {
    const atbus::node *n;
    const atbus::endpoint *ep;
    const atbus::connection *conn;
    std::string data;
    int status;
    int count;
    int add_endpoint_count;
    int remove_endpoint_count;
    int register_count;
    int availavle_count;

    node_reg_test_recv_msg_record_t()
        : n(NULL), ep(NULL), conn(NULL), status(0), count(0), add_endpoint_count(0), remove_endpoint_count(0), register_count(0),
          availavle_count(0) {}
};

static node_reg_test_recv_msg_record_t recv_msg_history;

static int node_reg_test_recv_msg_test_record_fn(const atbus::node &n, const atbus::endpoint *ep, const atbus::connection *conn,
                                                 const atbus::protocol::msg &m, const void *buffer, size_t len) {
    recv_msg_history.n      = &n;
    recv_msg_history.ep     = ep;
    recv_msg_history.conn   = conn;
    recv_msg_history.status = m.head.ret;
    ++recv_msg_history.count;

    if (NULL != buffer && len > 0) {
        recv_msg_history.data.assign(reinterpret_cast<const char *>(buffer), len);
    } else {
        recv_msg_history.data.clear();
    }

    return 0;
}

static int node_reg_test_add_endpoint_fn(const atbus::node &n, atbus::endpoint *ep, int) {
    ++recv_msg_history.add_endpoint_count;

    CASE_EXPECT_NE(NULL, ep);
    CASE_EXPECT_NE(n.get_self_endpoint(), ep);
    return 0;
}

static int node_reg_test_remove_endpoint_fn(const atbus::node &n, atbus::endpoint *ep, int) {
    ++recv_msg_history.remove_endpoint_count;

    CASE_EXPECT_NE(NULL, ep);
    CASE_EXPECT_NE(n.get_self_endpoint(), ep);
    return 0;
}

static int node_reg_test_on_register_fn(const atbus::node &, const atbus::endpoint *, const atbus::connection *, int) {
    ++recv_msg_history.register_count;
    return 0;
}

static int node_reg_test_on_available_fn(const atbus::node &, int status) {
    ++recv_msg_history.availavle_count;

    CASE_EXPECT_EQ(0, status);
    return 0;
}

// 主动reset流程测试
// 正常首发数据测试
CASE_TEST(atbus_node_reg, reset_and_send_tcp) {
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    {
        atbus::node::ptr_t node1 = atbus::node::create();
        atbus::node::ptr_t node2 = atbus::node::create();
        node1->on_debug          = node_reg_test_on_debug;
        node2->on_debug          = node_reg_test_on_debug;
        node1->set_on_error_handle(node_reg_test_on_error);
        node2->set_on_error_handle(node_reg_test_on_error);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_NOT_INITED, node1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_NOT_INITED, node2->start());

        node1->init(0x12345678, &conf);
        node2->init(0x12356789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->start());

        time_t proc_t = time(NULL);
        node1->poll();
        node2->poll();
        node1->proc(proc_t + 1, 0);
        node1->proc(proc_t + 1, 1);
        node2->proc(proc_t + 1, 0);
        node2->proc(proc_t + 1, 1);


        // 连接兄弟节点回调测试
        int check_ep_count = recv_msg_history.add_endpoint_count;
        node1->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node1->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);
        node2->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node2->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);

        node1->connect("ipv4://127.0.0.1:16388");

        UNITTEST_WAIT_UNTIL(conf.ev_loop, node1->is_endpoint_available(node2->get_id()) && node2->is_endpoint_available(node1->get_id()),
                            8000, 0) {}
        // in windows CI, connection will be closed sometimes, it will lead to add one endpoint more than one times
        CASE_EXPECT_LE(check_ep_count + 2, recv_msg_history.add_endpoint_count);

        // 兄弟节点消息转发测试
        std::string send_data;
        send_data.assign("abcdefg\0hello world!\n", sizeof("abcdefg\0hello world!\n") - 1);

        node1->poll();
        node2->poll();
        proc_t += 1000;
        node1->proc(proc_t, 0);
        node2->proc(proc_t, 0);


        int count = recv_msg_history.count;
        node2->set_on_recv_handle(node_reg_test_recv_msg_test_record_fn);
        CASE_EXPECT_TRUE(!!node2->get_on_recv_handle());
        node1->send_data(node2->get_id(), 0, send_data.data(), send_data.size());

        UNITTEST_WAIT_UNTIL(conf.ev_loop, count != recv_msg_history.count, 8000, 0) {}

        // check add endpoint callback
        CASE_EXPECT_EQ(send_data, recv_msg_history.data);
        // CASE_EXPECT_NE(NULL, node1->get_iostream_conf());

        check_ep_count = recv_msg_history.remove_endpoint_count;

        // reset
        CASE_EXPECT_EQ(0, node1->shutdown(0)); // shutdown - test, next proc() will call reset()
        CASE_EXPECT_EQ(0, node1->shutdown(0)); // shutdown - again

        UNITTEST_WAIT_UNTIL(conf.ev_loop, NULL == node1->get_endpoint(node2->get_id()) && NULL == node2->get_endpoint(node1->get_id()),
                            8000, 64) {
            ++proc_t;

            node1->proc(proc_t, 0);
            node2->proc(proc_t, 0);
        }

        node2->reset();

        // check remove endpoint callback
        // in windows CI, connection will be closed sometimes, it will lead to add one endpoint more than one times
        CASE_EXPECT_LE(check_ep_count + 2, recv_msg_history.remove_endpoint_count);

        CASE_EXPECT_EQ(NULL, node2->get_endpoint(node1->get_id()));
        CASE_EXPECT_EQ(NULL, node1->get_endpoint(node2->get_id()));
    }

    unit_test_setup_exit(&ev_loop);
}

// 被动析构流程测试
CASE_TEST(atbus_node_reg, destruct) {
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    {
        atbus::node::ptr_t node1 = atbus::node::create();
        atbus::node::ptr_t node2 = atbus::node::create();
        node1->on_debug          = node_reg_test_on_debug;
        node2->on_debug          = node_reg_test_on_debug;
        node1->set_on_error_handle(node_reg_test_on_error);
        node2->set_on_error_handle(node_reg_test_on_error);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_NOT_INITED, node1->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_NOT_INITED, node1->connect("ipv4://127.0.0.1:16388"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_NOT_INITED, node1->send_data(0x12345678, 213, NULL, 0, true));

        node1->init(0x12345678, &conf);
        node2->init(0x12356789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->start());

        time_t proc_t = time(NULL);
        node1->poll();
        node2->poll();
        node1->proc(proc_t + 1, 0);
        node2->proc(proc_t + 1, 0);

        node1->connect("ipv4://127.0.0.1:16388");

        UNITTEST_WAIT_UNTIL(conf.ev_loop, node1->is_endpoint_available(node2->get_id()) && node2->is_endpoint_available(node1->get_id()),
                            8000, 0) {}

        CASE_EXPECT_EQ(EN_ATBUS_ERR_BUFF_LIMIT, node1->send_data(0x12345678, 213, &conf, conf.msg_size + 1, true));

        for (int i = 0; i < 16; ++i) {
            uv_run(conf.ev_loop, UV_RUN_NOWAIT);
            CASE_THREAD_SLEEP_MS(4);
        }

        // reset shared_ptr and delete it
        node1.reset();

        ++proc_t;
        UNITTEST_WAIT_UNTIL(conf.ev_loop, NULL == node2->get_endpoint(0x12345678), 8000, 64) {
            ++proc_t;

            node2->proc(proc_t, 0);
        }

        CASE_EXPECT_EQ(NULL, node2->get_endpoint(0x12345678));
    }

    unit_test_setup_exit(&ev_loop);
}

// 注册成功流程测试 - 父子
CASE_TEST(atbus_node_reg, reg_pc_success) {
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    int check_ep_rm = recv_msg_history.remove_endpoint_count;
    {
        int old_register_count  = recv_msg_history.register_count;
        int old_available_count = recv_msg_history.availavle_count;

        atbus::node::ptr_t node_parent = atbus::node::create();
        atbus::node::ptr_t node_child  = atbus::node::create();
        node_parent->on_debug          = node_reg_test_on_debug;
        node_parent->set_on_error_handle(node_reg_test_on_error);
        node_parent->set_on_register_handle(node_reg_test_on_register_fn);
        node_parent->set_on_available_handle(node_reg_test_on_available_fn);

        node_child->on_debug = node_reg_test_on_debug;
        node_child->set_on_error_handle(node_reg_test_on_error);
        node_child->set_on_register_handle(node_reg_test_on_register_fn);
        node_child->set_on_available_handle(node_reg_test_on_available_fn);
        CASE_EXPECT_TRUE(!!node_child->get_on_register_handle());
        CASE_EXPECT_TRUE(!!node_child->get_on_available_handle());

        node_parent->init(0x12345678, &conf);

        conf.children_mask  = 8;
        conf.father_address = "ipv4://127.0.0.1:16387";
        node_child->init(0x12346789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->start());

        CASE_EXPECT_EQ(old_register_count, recv_msg_history.register_count);
        CASE_EXPECT_EQ(old_available_count + 1, recv_msg_history.availavle_count);

        // 父子节点注册回调测试
        int check_ep_count = recv_msg_history.add_endpoint_count;
        node_parent->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        CASE_EXPECT_TRUE(!!node_parent->get_on_add_endpoint_handle());
        node_parent->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);
        CASE_EXPECT_TRUE(!!node_parent->get_on_remove_endpoint_handle());
        node_child->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node_child->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);

        time_t proc_t = time(NULL);
        node_parent->poll();
        node_child->poll();
        node_parent->proc(proc_t + 1, 0);
        node_child->proc(proc_t + 1, 0);


        // 注册成功自动会有可用的端点
        UNITTEST_WAIT_UNTIL(
            conf.ev_loop,
            node_child->is_endpoint_available(node_parent->get_id()) && node_parent->is_endpoint_available(node_child->get_id()), 8000, 0) {
        }

        // in windows CI, connection will be closed sometimes, it will lead to add one endpoint more than one times
        CASE_EXPECT_LE(check_ep_count + 2, recv_msg_history.add_endpoint_count);
        CASE_EXPECT_LE(old_register_count + 2, recv_msg_history.register_count);
        CASE_EXPECT_LE(old_available_count + 2, recv_msg_history.availavle_count);

        // API - test
        {
            atbus::endpoint *test_ep     = NULL;
            atbus::connection *test_conn = NULL;
            node_parent->get_remote_channel(node_child->get_id(), &atbus::endpoint::get_data_connection, &test_ep, &test_conn);
            CASE_EXPECT_NE(NULL, test_ep);
            CASE_EXPECT_NE(NULL, test_conn);
        }

        // API - test
        {
            atbus::endpoint *test_ep     = NULL;
            atbus::connection *test_conn = NULL;
            node_child->get_remote_channel(node_parent->get_id(), &atbus::endpoint::get_data_connection, &test_ep, &test_conn);
            CASE_EXPECT_NE(NULL, test_ep);
            CASE_EXPECT_NE(NULL, test_conn);
        }

        // disconnect - parent and child
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->disconnect(0x12346789));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_ATNODE_NOT_FOUND, node_parent->disconnect(0x12346789));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->disconnect(0x12345678));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_ATNODE_NOT_FOUND, node_child->disconnect(0x12345678));
    }

    unit_test_setup_exit(&ev_loop);

    CASE_EXPECT_LE(check_ep_rm + 2, recv_msg_history.remove_endpoint_count);
}


// 注册成功流程测试 - 兄弟
CASE_TEST(atbus_node_reg, reg_bro_success) {
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 8;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    int check_ep_rm = recv_msg_history.remove_endpoint_count;
    {
        atbus::node::ptr_t node_1 = atbus::node::create();
        atbus::node::ptr_t node_2 = atbus::node::create();
        node_1->on_debug          = node_reg_test_on_debug;
        node_2->on_debug          = node_reg_test_on_debug;
        node_1->set_on_error_handle(node_reg_test_on_error);
        node_2->set_on_error_handle(node_reg_test_on_error);

        node_1->init(0x12345678, &conf);
        node_2->init(0x12356789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_1->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_2->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_2->start());

        // 兄弟节点注册回调测试
        int check_ep_count = recv_msg_history.add_endpoint_count;
        node_1->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node_1->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);
        node_2->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node_2->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);

        time_t proc_t = time(NULL);
        node_1->poll();
        node_2->poll();
        node_1->proc(proc_t + 1, 0);
        node_2->proc(proc_t + 1, 0);

        node_1->connect("ipv4://127.0.0.1:16388");

        // 注册成功自动会有可用的端点
        UNITTEST_WAIT_UNTIL(conf.ev_loop,
                            node_2->is_endpoint_available(node_1->get_id()) && node_1->is_endpoint_available(node_2->get_id()), 8000, 0) {}

        CASE_EXPECT_TRUE(node_2->is_endpoint_available(node_1->get_id()));
        CASE_EXPECT_TRUE(node_1->is_endpoint_available(node_2->get_id()));
        // in windows CI, connection will be closed sometimes, it will lead to add one endpoint more than one times
        CASE_EXPECT_LE(check_ep_count + 2, recv_msg_history.add_endpoint_count);

        // API - test
        {
            atbus::endpoint *test_ep     = NULL;
            atbus::connection *test_conn = NULL;
            node_1->get_remote_channel(node_2->get_id(), &atbus::endpoint::get_data_connection, &test_ep, &test_conn);
            CASE_EXPECT_NE(NULL, test_ep);
            CASE_EXPECT_NE(NULL, test_conn);
        }

        // API - test
        {
            atbus::endpoint *test_ep     = NULL;
            atbus::connection *test_conn = NULL;
            node_2->get_remote_channel(node_1->get_id(), &atbus::endpoint::get_data_connection, &test_ep, &test_conn);
            CASE_EXPECT_NE(NULL, test_ep);
            CASE_EXPECT_NE(NULL, test_conn);
        }

        // disconnect - parent and child
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_1->disconnect(0x12356789));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_ATNODE_NOT_FOUND, node_1->disconnect(0x12356789));
    }

    unit_test_setup_exit(&ev_loop);

    CASE_EXPECT_LE(check_ep_rm + 2, recv_msg_history.remove_endpoint_count);
}


static int g_node_test_on_shutdown_check_reason = 0;
static int node_test_on_shutdown(const atbus::node &, int reason) {
    if (0 == g_node_test_on_shutdown_check_reason) {
        ++g_node_test_on_shutdown_check_reason;
    } else {
        CASE_EXPECT_EQ(reason, g_node_test_on_shutdown_check_reason);
        g_node_test_on_shutdown_check_reason = 0;
    }

    return 0;
}

// 注册到父节点失败导致下线的流程测试
// 注册到子节点失败不会导致下线的流程测试
CASE_TEST(atbus_node_reg, conflict) {
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    // 只有发生冲突才会注册不成功，否则会无限重试注册父节点，直到其上线
    {
        atbus::node::ptr_t node_parent     = atbus::node::create();
        atbus::node::ptr_t node_child      = atbus::node::create();
        atbus::node::ptr_t node_child_fail = atbus::node::create();
        node_parent->on_debug              = node_reg_test_on_debug;
        node_child->on_debug               = node_reg_test_on_debug;
        node_child_fail->on_debug          = node_reg_test_on_debug;
        node_parent->set_on_error_handle(node_reg_test_on_error);
        node_child->set_on_error_handle(node_reg_test_on_error);
        node_child_fail->set_on_error_handle(node_reg_test_on_error);

        node_parent->init(0x12345678, &conf);

        conf.children_mask  = 8;
        conf.father_address = "ipv4://127.0.0.1:16387";
        node_child->init(0x12346789, &conf);
        // 子域冲突，注册失败
        node_child_fail->init(0x12346780, &conf);

        node_child->set_on_shutdown_handle(node_test_on_shutdown);
        CASE_EXPECT_TRUE(!!node_child->get_on_shutdown_handle());
        node_child_fail->set_on_shutdown_handle(node_test_on_shutdown);
        g_node_test_on_shutdown_check_reason = EN_ATBUS_ERR_ATNODE_INVALID_ID;

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->listen("ipv4://127.0.0.1:16388"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_fail->listen("ipv4://127.0.0.1:16389"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_fail->start());

        time_t proc_t = time(NULL) + 1;
        // 必然有一个失败的
        UNITTEST_WAIT_UNTIL(conf.ev_loop,
                            atbus::node::state_t::CREATED != node_child->get_state() &&
                                atbus::node::state_t::CREATED != node_child_fail->get_state(),
                            8000, 64) {
            node_parent->proc(proc_t, 0);
            node_child->proc(proc_t, 0);
            node_child_fail->proc(proc_t, 0);
            proc_t += conf.retry_interval;
        }

        for (int i = 0; i < 64; ++i) {
            CASE_THREAD_SLEEP_MS(4);
            uv_run(&ev_loop, UV_RUN_NOWAIT);
        }

        // 注册到子节点失败不会导致下线的流程测试
        CASE_EXPECT_TRUE(atbus::node::state_t::RUNNING == node_child->get_state() ||
                         atbus::node::state_t::RUNNING == node_child_fail->get_state());
        CASE_EXPECT_EQ(atbus::node::state_t::RUNNING, node_parent->get_state());
    }

    unit_test_setup_exit(&ev_loop);
}

// 对父节点重连失败不会导致下线的流程测试
// 对父节点断线重连的流程测试
CASE_TEST(atbus_node_reg, reconnect_father_failed) {
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    // 只有发生冲突才会注册不成功，否则会无限重试注册父节点，直到其上线
    {
        atbus::node::ptr_t node_parent = atbus::node::create();
        atbus::node::ptr_t node_child  = atbus::node::create();
        node_parent->on_debug          = node_reg_test_on_debug;
        node_child->on_debug           = node_reg_test_on_debug;
        node_parent->set_on_error_handle(node_reg_test_on_error);
        CASE_EXPECT_TRUE(!!node_parent->get_on_error_handle());
        node_child->set_on_error_handle(node_reg_test_on_error);

        node_parent->init(0x12345678, &conf);

        conf.children_mask  = 8;
        conf.father_address = "ipv4://127.0.0.1:16387";
        node_child->init(0x12346789, &conf);

        node_child->set_on_shutdown_handle(node_test_on_shutdown);
        g_node_test_on_shutdown_check_reason = EN_ATBUS_ERR_ATNODE_INVALID_ID;

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->start());

        time_t proc_t = time(NULL) + 1;
        // 先等连接成功
        UNITTEST_WAIT_UNTIL(conf.ev_loop, atbus::node::state_t::RUNNING == node_child->get_state(), 8000, 64) {
            node_parent->proc(proc_t, 0);
            node_child->proc(proc_t, 0);
            ++proc_t;
        }

        // 关闭父节点
        node_parent->reset();

        // 重连父节点，但是连接不成功也不会导致下线
        // 连接过程中的转态变化
        size_t retry_times = 0;
        UNITTEST_WAIT_IF(conf.ev_loop, atbus::node::state_t::RUNNING == node_child->get_state() || retry_times < 16, 8000, 64) {
            proc_t += conf.retry_interval + 1;

            node_child->proc(proc_t, 0);

            if (atbus::node::state_t::RUNNING != node_child->get_state()) {
                ++retry_times;
                CASE_EXPECT_TRUE(atbus::node::state_t::LOST_PARENT == node_child->get_state() ||
                                 atbus::node::state_t::CONNECTING_PARENT == node_child->get_state());
                CASE_EXPECT_NE(atbus::node::state_t::CREATED, node_child->get_state());
                CASE_EXPECT_NE(atbus::node::state_t::INITED, node_child->get_state());
            }

            CASE_THREAD_SLEEP_MS(4);
            uv_run(&ev_loop, UV_RUN_NOWAIT);
            uv_run(&ev_loop, UV_RUN_NOWAIT);
            uv_run(&ev_loop, UV_RUN_NOWAIT);
        }

        // 父节点断线重连测试
        // 子节点断线后重新注册测试
        conf.children_mask  = 16;
        conf.father_address = "";
        node_parent->init(0x12345678, &conf);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());

        UNITTEST_WAIT_IF(
            conf.ev_loop,
            atbus::node::state_t::RUNNING != node_child->get_state() || NULL == node_parent->get_endpoint(node_child->get_id()), 8000, 64) {
            proc_t += conf.retry_interval;
            node_parent->proc(proc_t, 0);
            node_child->proc(proc_t, 0);
        }

        {
            atbus::endpoint *ep1 = node_child->get_endpoint(node_parent->get_id());
            atbus::endpoint *ep2 = node_parent->get_endpoint(node_child->get_id());

            CASE_EXPECT_NE(NULL, ep1);
            CASE_EXPECT_NE(NULL, ep2);
            CASE_EXPECT_EQ(atbus::node::state_t::RUNNING, node_child->get_state());
        }

        // 注册到子节点失败不会导致下线的流程测试
        CASE_EXPECT_EQ(atbus::node::state_t::RUNNING, node_parent->get_state());
    }

    unit_test_setup_exit(&ev_loop);
}

// API: hostname
CASE_TEST(atbus_node_reg, set_hostname) {
    std::string old_hostname = ::atbus::node::get_hostname();
    CASE_EXPECT_TRUE(atbus::node::set_hostname("test-host-for", true));
    CASE_EXPECT_EQ(std::string("test-host-for"), ::atbus::node::get_hostname());
    CASE_EXPECT_TRUE(atbus::node::set_hostname(old_hostname, true));
}

// 正常首发数据测试 -- 内存通道
CASE_TEST(atbus_node_reg, mem_and_send) {
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    const size_t memory_chan_len = conf.recv_buffer_size;
    char *memory_chan_buf        = reinterpret_cast<char *>(malloc(memory_chan_len));

    {
        atbus::node::ptr_t node1 = atbus::node::create();
        atbus::node::ptr_t node2 = atbus::node::create();
        node1->on_debug          = node_reg_test_on_debug;
        node2->on_debug          = node_reg_test_on_debug;
        node1->set_on_error_handle(node_reg_test_on_error);
        node2->set_on_error_handle(node_reg_test_on_error);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_NOT_INITED, node1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_NOT_INITED, node2->start());

        node1->init(0x12345678, &conf);
        node2->init(0x12356789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->listen("ipv4://127.0.0.1:16388"));
        char mem_chan_addr[64] = {0};
        UTIL_STRFUNC_SNPRINTF(mem_chan_addr, sizeof(mem_chan_addr), "mem://0x%llx", reinterpret_cast<unsigned long long>(memory_chan_buf));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->listen(mem_chan_addr));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->start());

        time_t proc_t = time(NULL);
        node1->poll();
        node2->poll();
        node1->proc(proc_t + 1, 0);
        node1->proc(proc_t + 1, 1);
        node2->proc(proc_t + 1, 0);
        node2->proc(proc_t + 1, 1);


        // 连接兄弟节点回调测试
        int check_ep_count = recv_msg_history.add_endpoint_count;
        node1->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node1->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);
        node2->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node2->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);

        node1->connect("ipv4://127.0.0.1:16388");

        UNITTEST_WAIT_UNTIL(conf.ev_loop, node1->is_endpoint_available(node2->get_id()) && node2->is_endpoint_available(node1->get_id()),
                            8000, 1000) {
            ++proc_t;
            node1->poll();
            node1->proc(proc_t, 2);
            node2->poll();
            node2->proc(proc_t, 2);
        }

        // wait memory channel to complete
        for (time_t i = 1; i <= 32; ++i) {
            node1->proc(proc_t, i * 16);
            node2->proc(proc_t, i * 16);
        }

        // API - test - 数据通道优先应该是内存通道
        {
            atbus::endpoint *test_ep     = NULL;
            atbus::connection *test_conn = NULL;
            node1->get_remote_channel(node2->get_id(), &atbus::endpoint::get_data_connection, &test_ep, &test_conn);
            CASE_EXPECT_NE(NULL, test_ep);
            CASE_EXPECT_NE(NULL, test_conn);

            if (NULL != test_conn) {
                CASE_EXPECT_TRUE(test_conn->is_connected());
                // connect的节点是不注册REG_PROC的
                CASE_EXPECT_FALSE(test_conn->check_flag(atbus::connection::flag_t::REG_PROC));
            }
        }

        // in windows CI, connection will be closed sometimes, it will lead to add one endpoint more than one times
        CASE_EXPECT_LE(check_ep_count + 2, recv_msg_history.add_endpoint_count);

        // 兄弟节点消息转发测试
        std::string send_data;
        send_data.assign("abcdefg\0hello world!\n", sizeof("abcdefg\0hello world!\n") - 1);

        node1->poll();
        node2->poll();
        proc_t += 1;
        node1->proc(proc_t, 0);
        node2->proc(proc_t, 0);


        int count = recv_msg_history.count;
        node2->set_on_recv_handle(node_reg_test_recv_msg_test_record_fn);
        CASE_EXPECT_TRUE(!!node2->get_on_recv_handle());
        CASE_EXPECT_EQ(0, node1->send_data(node2->get_id(), 0, send_data.data(), send_data.size()));

        proc_t += 1;
        node1->proc(proc_t, 0);
        node2->proc(proc_t, 0);

        time_t proc_sum = 0;
        UNITTEST_WAIT_UNTIL(conf.ev_loop, count != recv_msg_history.count, 8000, 50) {
            proc_sum += 50;
            if (proc_sum >= 1000) {
                ++proc_t;
                proc_sum = 0;
            }
            node1->poll();
            node1->proc(proc_t, proc_sum * 1000);
            node2->poll();
            node2->proc(proc_t, proc_sum * 1000);
        }

        // check add endpoint callback
        CASE_EXPECT_EQ(send_data, recv_msg_history.data);
        // CASE_EXPECT_NE(NULL, node1->get_iostream_conf());

        check_ep_count = recv_msg_history.remove_endpoint_count;

        // reset
        CASE_EXPECT_EQ(0, node1->shutdown(0)); // shutdown - test, next proc() will call reset()
        CASE_EXPECT_EQ(0, node1->shutdown(0)); // shutdown - again

        UNITTEST_WAIT_UNTIL(conf.ev_loop, NULL == node1->get_endpoint(node2->get_id()) && NULL == node2->get_endpoint(node1->get_id()),
                            8000, 64) {
            ++proc_t;

            node1->proc(proc_t, 0);
            node2->proc(proc_t, 0);
        }

        node2->reset();

        // check remove endpoint callback
        // in windows CI, connection will be closed sometimes, it will lead to add one endpoint more than one times
        CASE_EXPECT_LE(check_ep_count + 2, recv_msg_history.remove_endpoint_count);

        CASE_EXPECT_EQ(NULL, node2->get_endpoint(node1->get_id()));
        CASE_EXPECT_EQ(NULL, node1->get_endpoint(node2->get_id()));
    }

    unit_test_setup_exit(&ev_loop);

    free(memory_chan_buf);
}

#if defined(ATBUS_CHANNEL_SHM) && ATBUS_CHANNEL_SHM

static bool node_reg_test_is_shm_available(const atbus::node::conf_t &conf) {
    // check if /proc/sys/kernel/shmmax exists
    if (!util::file_system::is_exist("/proc/sys/kernel/shmmax")) {
        return false;
    }

    std::string sz_contest;
    util::file_system::get_file_content(sz_contest, "/proc/sys/kernel/shmmax");
    return util::string::to_int<size_t>(sz_contest.c_str()) >= conf.recv_buffer_size;
}

// 正常首发数据测试 -- 共享内存
CASE_TEST(atbus_node_reg, shm_and_send) {
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);

    if (!node_reg_test_is_shm_available(conf)) {
        return;
    }

    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    {
        atbus::node::ptr_t node1 = atbus::node::create();
        atbus::node::ptr_t node2 = atbus::node::create();
        node1->on_debug          = node_reg_test_on_debug;
        node2->on_debug          = node_reg_test_on_debug;
        node1->set_on_error_handle(node_reg_test_on_error);
        node2->set_on_error_handle(node_reg_test_on_error);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_NOT_INITED, node1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_NOT_INITED, node2->start());

        node1->init(0x12345678, &conf);
        node2->init(0x12356789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->listen("ipv4://127.0.0.1:16388"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->listen("shm://0x23456789"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->start());

        time_t proc_t = time(NULL);
        node1->poll();
        node2->poll();
        node1->proc(proc_t + 1, 0);
        node1->proc(proc_t + 1, 1);
        node2->proc(proc_t + 1, 0);
        node2->proc(proc_t + 1, 1);


        // 连接兄弟节点回调测试
        int check_ep_count = recv_msg_history.add_endpoint_count;
        node1->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node1->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);
        node2->set_on_add_endpoint_handle(node_reg_test_add_endpoint_fn);
        node2->set_on_remove_endpoint_handle(node_reg_test_remove_endpoint_fn);

        node1->connect("ipv4://127.0.0.1:16388");

        UNITTEST_WAIT_UNTIL(conf.ev_loop, node1->is_endpoint_available(node2->get_id()) && node2->is_endpoint_available(node1->get_id()),
                            8000, 1000) {
            ++proc_t;
            node1->poll();
            node1->proc(proc_t, 2);
            node2->poll();
            node2->proc(proc_t, 2);
        }

        // wait memory channel to complete
        for (time_t i = 1; i <= 32; ++i) {
            node1->proc(proc_t, i * 16);
            node2->proc(proc_t, i * 16);
        }

        // API - test - 数据通道优先应该是共享内存通道
        {
            atbus::endpoint *test_ep     = NULL;
            atbus::connection *test_conn = NULL;
            node1->get_remote_channel(node2->get_id(), &atbus::endpoint::get_data_connection, &test_ep, &test_conn);
            CASE_EXPECT_NE(NULL, test_ep);
            CASE_EXPECT_NE(NULL, test_conn);

            if (NULL != test_conn) {
                CASE_EXPECT_TRUE(test_conn->is_connected());
                // connect的节点是不注册REG_PROC的
                CASE_EXPECT_FALSE(test_conn->check_flag(atbus::connection::flag_t::REG_PROC));
            }
        }

        // in windows CI, connection will be closed sometimes, it will lead to add one endpoint more than one times
        CASE_EXPECT_LE(check_ep_count + 2, recv_msg_history.add_endpoint_count);

        // 兄弟节点消息转发测试
        std::string send_data;
        send_data.assign("abcdefg\0hello world!\n", sizeof("abcdefg\0hello world!\n") - 1);

        node1->poll();
        node2->poll();
        proc_t += 1;
        node1->proc(proc_t, 0);
        node2->proc(proc_t, 0);


        int count = recv_msg_history.count;
        node2->set_on_recv_handle(node_reg_test_recv_msg_test_record_fn);
        CASE_EXPECT_TRUE(!!node2->get_on_recv_handle());
        CASE_EXPECT_EQ(0, node1->send_data(node2->get_id(), 0, send_data.data(), send_data.size()));

        proc_t += 1;
        node1->proc(proc_t, 0);
        node2->proc(proc_t, 0);

        time_t proc_sum = 0;
        UNITTEST_WAIT_UNTIL(conf.ev_loop, count != recv_msg_history.count, 8000, 50) {
            proc_sum += 50;
            if (proc_sum >= 1000) {
                ++proc_t;
                proc_sum = 0;
            }
            node1->poll();
            node1->proc(proc_t, proc_sum * 1000);
            node2->poll();
            node2->proc(proc_t, proc_sum * 1000);
        }

        // check add endpoint callback
        CASE_EXPECT_EQ(send_data, recv_msg_history.data);
        // CASE_EXPECT_NE(NULL, node1->get_iostream_conf());

        check_ep_count = recv_msg_history.remove_endpoint_count;

        // reset
        CASE_EXPECT_EQ(0, node1->shutdown(0)); // shutdown - test, next proc() will call reset()
        CASE_EXPECT_EQ(0, node1->shutdown(0)); // shutdown - again

        UNITTEST_WAIT_UNTIL(conf.ev_loop, NULL == node1->get_endpoint(node2->get_id()) && NULL == node2->get_endpoint(node1->get_id()),
                            8000, 64) {
            ++proc_t;

            node1->proc(proc_t, 0);
            node2->proc(proc_t, 0);
        }

        node2->reset();

        // check remove endpoint callback
        // in windows CI, connection will be closed sometimes, it will lead to add one endpoint more than one times
        CASE_EXPECT_LE(check_ep_count + 2, recv_msg_history.remove_endpoint_count);

        CASE_EXPECT_EQ(NULL, node2->get_endpoint(node1->get_id()));
        CASE_EXPECT_EQ(NULL, node1->get_endpoint(node2->get_id()));
    }

    unit_test_setup_exit(&ev_loop);
}
#endif