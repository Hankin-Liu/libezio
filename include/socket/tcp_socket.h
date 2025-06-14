/****************************************************************************************
 * @file tcp_socket.h
 * @brief tcp listener and connector
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <netinet/in.h>
#include <memory>
#include <string>
#include <list>
#include <atomic>
#include <vector>
#include <set>
#include "../platform_define.h"
#include "../event/event_base.h"
#include "../type_def.h"

namespace ezio {
    namespace event {
        class event_service;
    }
    namespace socket {
        typedef uint32_t chan_t;
        constexpr uint32_t MAX_WAIT_CNT = 20;

        enum class TCP_STATE_MACHINE : uint16_t
        {
            DEF = 0,
            CONNECTED,
            CLOSED,
        };

        struct connection_info
        {
            connection_info() = default;
            connection_info(const sockaddr_storage& addr, ezio::event::fd_t fd, chan_t chan_id)
                : addr_(addr), fd_(fd), chan_id_(chan_id)
            {
            }
            inline const int64_t get_error_no() const {
                return errno_;
            }
            const std::string get_ip_addr() const;
            const uint32_t get_port() const;

            struct sockaddr_storage addr_{};
            ezio::event::fd_t fd_{};
            chan_t chan_id_{ 0 };
            int64_t errno_{ 0 };
        };
        typedef std::function<void(const connection_info&)> tcp_connect_callback;
        typedef std::function<void(const connection_info&)> tcp_close_callback;
        typedef std::function<void(const connection_info&, int64_t)> tcp_error_callback;
        typedef std::function<void(const connection_info&, ::iovec iov)> tcp_data_callback;

        enum class TCP_CONNECT_MODE : uint16_t 
        {
            LISTEN = 0,
            CONNECT = 1
        };

        enum class TCP_STATE : uint16_t
        {
            DEF = 0,
            CONNECTED = 1,
            CLOSED = 2,
            INPROGRESS = 3,
        };

        struct keep_alive_config
        {
            bool is_enabled_{ false };
            uint32_t keepalive_idle_s_{ 15 };
            uint32_t keepalive_interval_s_{ 5 };
            uint32_t keepalive_count_{ 3 };
            uint32_t user_timeout_ms_{ 30000 };
        };

        struct handler
        {
            tcp_connect_callback cb_conn_{ nullptr };
            tcp_close_callback cb_close_{ nullptr };
            tcp_data_callback cb_data_{ nullptr };
            tcp_error_callback cb_error_{ nullptr };
        };

        struct tcp_config
        {
            tcp_config() {
            }
            TCP_CONNECT_MODE mode_{TCP_CONNECT_MODE::LISTEN};
            std::string addr_{""};
            keep_alive_config keep_alive_conf_;
            uint32_t max_connect_cnt_{ UINT32_MAX };
        //    uint64_t busi_polling_weight_threshold_{ UINT32_MAX };
            handler handlers_{};
            uint32_t block_size_{ 4096 };
            uint32_t block_count_{ 256 };

            void reg_handler(const tcp_connect_callback& cb_conn,
                             const tcp_close_callback& cb_close,
                             const tcp_data_callback& cb_data,
                             const tcp_error_callback& cb_error)
            {
                handlers_.cb_conn_ = cb_conn;
                handlers_.cb_close_ = cb_close;
                handlers_.cb_data_ = cb_data;
                handlers_.cb_error_ = cb_error;
            }
        };

        class tcp_event;
        struct tcp_shared_data
        {
        public:
            tcp_shared_data(const tcp_config& conf, ezio::event::event_service* evt_service_ptr);
            ~tcp_shared_data();

            inline const tcp_config& get_tcp_config() const {
                return tcp_config_;
            }
            inline const keep_alive_config& get_tcp_keep_alive_config() const {
                return tcp_config_.keep_alive_conf_;
            }

            int32_t get_buffer_id();
            inline void add_buffer_id(int32_t buf_id) {
                buffer_id_list_.push_back(buf_id);
            }
            //inline void add_weight() {
            //    ++busi_polling_weight_;
            //}
            inline void add_closed_chan_id(chan_t chan_id) {
                closed_chan_ids_.push_back(chan_id);
            }
            inline std::list<chan_t>& get_closed_chan_ids() {
                return closed_chan_ids_;
            }

            inline void reg_timer_req(const std::shared_ptr<tcp_event>& tcp_ptr) {
                req_timer_set_.insert(tcp_ptr);
            }
            inline void remove_timer_req(const std::shared_ptr<tcp_event>& tcp_ptr) {
                req_timer_set_.erase(tcp_ptr);
            }
            inline const std::set<std::shared_ptr<tcp_event>>& get_req_timer_set() const {
                return req_timer_set_;
            }
        private:
            std::list<int32_t> buffer_id_list_{};
            tcp_config tcp_config_{};
            ezio::event::event_service* evt_service_ptr_{ nullptr };
            std::list<chan_t> closed_chan_ids_{};
            std::set<std::shared_ptr<tcp_event>> req_timer_set_{};

            // if tcp_socket has destructed, this pointer will be used or not?
            //tcp_socket* tcp_socket_ptr_{ nullptr };
            //std::weak_ptr<tcp_socket> tcp_socket_ptr_{};
            //uint64_t busi_polling_weight_{ 0 };
        };

        class tcp_event : public ezio::event::event_base
        {
            friend class tcp_socket;
            constexpr tcp_event(tcp_event&) = delete;
            tcp_event& operator=(tcp_event&) = delete;
            public:
                tcp_event();
                virtual ~tcp_event();
            private:
                virtual void handle_read_ready(ezio::event::fd_t fd) override;
                virtual void handle_write_ready(ezio::event::fd_t fd) override;
                virtual void on_started(bool is_success) override;
                int32_t open(ezio::event::event_service* evt_service_ptr, const connection_info& conn_info,
                             const std::shared_ptr<tcp_shared_data>& shared_data_ptr);
                int32_t close();
                int32_t try_connect(bool is_first_try = false);
                void check_and_process_tcp_connected();
                bool check_tcp_connect();
                bool is_tcp_connect();
                void create_timer_for_reconnect();
                void close_reconnect_timer();
                void remove_event(bool is_close_fd = true);
                inline bool is_timer_started() const {
                    return cur_timer_trigger_times_ != -1;
                }
                void handle_connect_timeout();
                void on_read_tcp_msg(int32_t ret, const ::iovec* iov_ptr, uint32_t iov_cnt);
                void on_timer();
            private:
                connection_info conn_info_{};
                uint32_t handler_index_{ 0 };
                std::shared_ptr<tcp_shared_data> shared_data_ptr_{ nullptr };
                TCP_STATE tcp_state_{ TCP_STATE::DEF };
                int32_t buf_id_{ -1 };
                int32_t cur_timer_trigger_times_{ -1 };
        };

        class tcp_socket final : public ezio::event::event_base
        {
            constexpr tcp_socket(tcp_socket&) = delete;
            tcp_socket& operator=(tcp_socket&) = delete;
            friend tcp_event;
            public:
                tcp_socket();
                virtual ~tcp_socket();
            public:
                int32_t open(ezio::event::event_service* evt_service_ptr, const tcp_config& conf);
                int32_t send(const connection_info& conn, ::iovec* data_iov, uint32_t iov_cnt);
                int32_t async_send(const connection_info& conn, ::iovec* data_iov, uint32_t iov_cnt, const std::function<void(int32_t)>& cb = nullptr);
                int32_t cancel_async_send(const connection_info& conn, const std::function<void(int32_t)>& cb);
                int32_t close();
                int32_t close_tcp_channel(chan_t chan_id);
            private:
                int32_t open_listener(const tcp_config& conf);
                int32_t open_connector();
                int32_t on_timer();
                void on_accept(int32_t ret, const ezio::event::sock_info& info);
                virtual void handle_read_ready(ezio::event::fd_t fd) override;
                int32_t make_listen_fd();
                static chan_t get_new_chan_id();
                inline bool is_reopen_listener() const {
                    return (! is_enabled_ && connect_cnt_ < shared_data_ptr_->get_tcp_config().max_connect_cnt_);
                }
                int32_t reopen_listener();
                void remove_event();
            private:
                ezio::event::fd_t fd_{}; ///< tcp listener fd or connector fd
                std::unordered_map<chan_t, tcp_event::pointer_t> chan_id_to_connections_{};
                static std::atomic<chan_t> chan_id_; ///< used to generate unique channel id for tcp object
                chan_t connect_chan_id_{ 0 }; ///< connector's channel id
                struct sockaddr_storage sock_addr_{};
                bool is_enabled_{ true };
                ezio::event::sock_info sock_info_buffer_{};
                uint32_t connect_cnt_{ 0 }; ///< current connected TCP count
                int32_t timer_id_{ -1 };
                std::shared_ptr<tcp_shared_data> shared_data_ptr_{ nullptr };
        };
    }
}
