﻿/**
 * atbus_connection.h
 *
 *  Created on: 2015年11月20日
 *      Author: owent
 */

#pragma once

#ifndef LIBATBUS_CONNECTION_H
#define LIBATBUS_CONNECTION_H

#pragma once

#include <bitset>
#include <ctime>
#include <list>

#ifdef _MSC_VER
#include <WinSock2.h>
#endif

#include "std/explicit_declare.h"
#include "std/smart_ptr.h"

#include "design_pattern/noncopyable.h"

#include "detail/libatbus_channel_export.h"
#include "detail/libatbus_config.h"
#include "detail/libatbus_error.h"

namespace atbus {
    namespace protocol {
        struct msg;
    }

    class node;
    class endpoint;

    class connection UTIL_CONFIG_FINAL : public util::design_pattern::noncopyable {
    public:
        typedef std::shared_ptr<connection> ptr_t;

        /** 并没有非常复杂的状态切换，所以没有引入状态机 **/
        typedef struct {
            enum type {
                DISCONNECTED = 0, /** 未连接 **/
                CONNECTING,       /** 正在连接 **/
                HANDSHAKING,      /** 正在握手 **/
                CONNECTED,        /** 已连接 **/
                DISCONNECTING,    /** 正在断开连接 **/
            };
        } state_t;

        typedef struct {
            enum type {
                REG_PROC = 0,      /** 注册了proc记录到node，清理的时候需要移除 **/
                REG_FD,            /** 关联了fd到node或endpoint，清理的时候需要移除 **/
                ACCESS_SHARE_ADDR, /** 共享内部地址（内存通道的地址共享） **/
                ACCESS_SHARE_HOST, /** 共享物理机（共享内存通道的物理机共享） **/
                RESETTING,         /** 正在执行重置（防止递归死循环） **/
                DESTRUCTING,       /** 正在执行析构（屏蔽某些接口） **/
                LISTEN_FD,         /** 是否是用于listen的连接 **/
                MAX
            };
        } flag_t;

        struct stat_t {
            size_t push_start_times;
            size_t push_start_size;
            size_t push_success_times;
            size_t push_success_size;
            size_t push_failed_times;
            size_t push_failed_size;

            size_t pull_times;
            size_t pull_size;
        };

    private:
        connection();

    public:
        static ptr_t create(node *owner);

        ~connection();

        void reset();

        /**
         * @brief 执行一帧
         * @param sec 当前时间-秒
         * @param usec 当前时间-微秒
         * @return 本帧处理的消息数
         */
        int proc(node &n, time_t sec, time_t usec);

        /**
         * @brief 监听数据接收地址
         * @param addr 监听地址
         * @param is_caddr 是否是控制节点
         * @return 0或错误码
         */
        int listen(const char *addr);

        /**
         * @brief 连接到目标地址
         * @param addr 连接目标地址
         * @return 0或错误码
         */
        int connect(const char *addr);

        /**
         * @brief 断开连接
         * @param id 目标ID
         * @return 0或错误码
         */
        int disconnect();


        /**
         * @brief 发送数据
         * @param buffer 数据块地址
         * @param s 数据块长度
         * @return 0或错误码
         * @note 接收端收到的数据很可能不是地址对齐的，所以这里不建议发送内存数据
         *       如果非要发送内存数据的话，一定要memcpy，不能直接类型转换，除非手动设置了地址对齐规则
         */
        int push(const void *buffer, size_t s);

        /**
         * @brief 获取连接的地址
         */
        inline const channel::channel_address_t &get_address() const { return address_; };

        /**
         * @brief 是否已连接
         */
        bool is_connected() const;

        /**
         * @brief 获取关联的端点
         */
        endpoint *get_binding();

        /**
         * @brief 获取关联的端点
         */
        const endpoint *get_binding() const;

        inline state_t::type get_status() const { return state_; }
        inline bool check_flag(flag_t::type f) const { return flags_.test(f); }

        /**
         * @brief 获取自身的智能指针
         * @note 在析构阶段这个接口无效
         */
        ptr_t watch() const;

        /** 是否正在连接、或者握手或者已连接 **/
        bool is_running() const;

        inline const stat_t &get_statistic() const { return stat_; }

    public:
        static void iostream_on_listen_cb(channel::io_stream_channel *channel, channel::io_stream_connection *connection, int status,
                                          void *buffer, size_t s);
        static void iostream_on_connected_cb(channel::io_stream_channel *channel, channel::io_stream_connection *connection, int status,
                                             void *buffer, size_t s);

        static void iostream_on_recv_cb(channel::io_stream_channel *channel, channel::io_stream_connection *connection, int status,
                                        void *buffer, size_t s);
        static void iostream_on_accepted(channel::io_stream_channel *channel, channel::io_stream_connection *connection, int status,
                                         void *buffer, size_t s);
        static void iostream_on_connected(channel::io_stream_channel *channel, channel::io_stream_connection *connection, int status,
                                          void *buffer, size_t s);
        static void iostream_on_disconnected(channel::io_stream_channel *channel, channel::io_stream_connection *connection, int status,
                                             void *buffer, size_t s);
        static void iostream_on_written(channel::io_stream_channel *channel, channel::io_stream_connection *connection, int status,
                                        void *buffer, size_t s);

#ifdef ATBUS_CHANNEL_SHM
        static int shm_proc_fn(node &n, connection &conn, time_t sec, time_t usec);

        static int shm_free_fn(node &n, connection &conn);

        static int shm_push_fn(connection &conn, const void *buffer, size_t s);
#endif

        static int mem_proc_fn(node &n, connection &conn, time_t sec, time_t usec);

        static int mem_free_fn(node &n, connection &conn);

        static int mem_push_fn(connection &conn, const void *buffer, size_t s);

        static int ios_free_fn(node &n, connection &conn);

        static int ios_push_fn(connection &conn, const void *buffer, size_t s);

        static bool unpack(void *res, connection &conn, atbus::protocol::msg &m, void *buffer, size_t s);

    private:
        state_t::type state_;
        channel::channel_address_t address_;
        std::bitset<flag_t::MAX> flags_;

        // 这里不用智能指针是为了该值在上层对象（node或者endpoint）析构时仍然可用
        node *owner_;
        endpoint *binding_;
        std::weak_ptr<connection> watcher_;

        typedef struct {
            channel::mem_channel *channel;
            void *buffer;
            size_t len;
        } conn_data_mem;

#ifdef ATBUS_CHANNEL_SHM
        typedef struct {
            channel::shm_channel *channel;
            key_t shm_key;
            size_t len;
        } conn_data_shm;
#endif

        typedef struct {
            channel::io_stream_channel *channel;
            channel::io_stream_connection *conn;
        } conn_data_ios;

        typedef struct {
            typedef union {
                conn_data_mem mem;
#ifdef ATBUS_CHANNEL_SHM
                conn_data_shm shm;
#endif
                conn_data_ios ios_fd;
            } shared_t;
            typedef int (*proc_fn_t)(node &n, connection &conn, time_t sec, time_t usec);
            typedef int (*free_fn_t)(node &n, connection &conn);
            typedef int (*push_fn_t)(connection &conn, const void *buffer, size_t s);

            shared_t shared;
            proc_fn_t proc_fn;
            free_fn_t free_fn;
            push_fn_t push_fn;
        } connection_data_t;
        connection_data_t conn_data_;
        stat_t stat_;

        friend class endpoint;
    };
} // namespace atbus

#endif /* LIBATBUS_CONNECTION_H_ */
