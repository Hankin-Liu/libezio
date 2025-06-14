/****************************************************************************************
 * @file tcp_socket.cpp
 * @brief tcp listener and connector
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include "../../include/platform_define.h"
#if defined _OS_LINUX
#include <arpa/inet.h>
#include <netinet/tcp.h>
#endif
#include <memory>
#include "../../include/socket/tcp_socket.h"
#include "../../include/util/macros_func.h"
#include "../../include/util/util.h"
#include "../../include/event_service.h"

using namespace ezio::event;

namespace ezio {
    namespace socket {
        std::atomic<chan_t> tcp_socket::chan_id_{ 0 };

        static int32_t get_tcp_socket_fd()
        {
            auto fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
            STABLE_INFRA_CHECK_SUC(fd > 0, -1);
            return fd;
        }

        static int32_t set_keep_alive(int32_t fd, const keep_alive_config& conf)
        {
            STABLE_INFRA_CHECK_SUC(fd > 0, -1);
            STABLE_INFRA_IF_TRUE_RETURN_CODE(! conf.is_enabled_, 0);
            int32_t keepalive_enable = 1;
            auto ret = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void*)&(keepalive_enable),
                                  sizeof(keepalive_enable));
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (void*)&(conf.keepalive_idle_s_),
                             sizeof(conf.keepalive_idle_s_));
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (void*)&(conf.keepalive_interval_s_),
                             sizeof(conf.keepalive_interval_s_));
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (void*)&(conf.keepalive_count_),
                             sizeof(conf.keepalive_count_));
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            ret = setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, (void*)&(conf.user_timeout_ms_),
                             sizeof(conf.user_timeout_ms_));
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            return 0;
        }

        tcp_shared_data::tcp_shared_data(const tcp_config& conf, event_service* evt_service_ptr)
            : tcp_config_(conf), evt_service_ptr_(evt_service_ptr)
        {
        }

        tcp_shared_data::~tcp_shared_data()
        {
            for (auto buf_id : buffer_id_list_) {
                evt_service_ptr_->unregister_ring_buffer(buf_id);
            }
        }

        int32_t tcp_shared_data::get_buffer_id()
        {
            if (buffer_id_list_.empty()) {
                return evt_service_ptr_->register_ring_buffer(tcp_config_.block_size_, tcp_config_.block_count_);
            }
            auto buf_id = buffer_id_list_.front();
            buffer_id_list_.pop_front();
            return buf_id;
        }

        tcp_event::tcp_event()
        {
        }

        tcp_event::~tcp_event()
        {
        }

        int32_t tcp_event::open(event_service* evt_service_ptr, const connection_info& conn_info,
                const std::shared_ptr<tcp_shared_data>& shared_data_ptr)
        {
            set_evt_service_ptr(evt_service_ptr);
            shared_data_ptr_ = shared_data_ptr;
            const auto& conf = shared_data_ptr_->get_tcp_config();
            conn_info_ = conn_info;
            buf_id_ = shared_data_ptr_->get_buffer_id();
            STABLE_INFRA_CHECK_SUC(0 <= buf_id_, -1);
            if (conf.mode_ == TCP_CONNECT_MODE::CONNECT) {
                constexpr bool is_first_try = true;
                auto ret = try_connect(is_first_try);
                STABLE_INFRA_CHECK_SUC(0 == ret, -1);
            } else {
                bool is_tcp_connected = check_tcp_connect();
                STABLE_INFRA_CHECK_SUC(is_tcp_connected, -1);
                uint16_t evt = EV_READ;
                auto tp = std::make_tuple(conn_info_.fd_, evt);
                fds_evts_.emplace_back(tp);
                evt_service_ptr_->add_event(shared_from_this()); // start monitor this new fd
                tcp_state_ = TCP_STATE::CONNECTED;
                // call user function
                conf.handlers_.cb_conn_(conn_info_);
            }
            return 0;
        }

        int32_t tcp_event::close()
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(tcp_state_ == TCP_STATE::CLOSED, 0);
            ezio::event::event_base::close(); // parent clear
            if (conn_info_.fd_.is_valid()) {
                conn_info_.fd_.close();
            }
            close_reconnect_timer();
            if (buf_id_ != -1) {
                shared_data_ptr_->add_buffer_id(buf_id_);
                buf_id_ = -1;
            }
            bool call_close_cb = (tcp_state_ == TCP_STATE::CONNECTED) ? true: false ;
            tcp_state_ = TCP_STATE::CLOSED;
            if (call_close_cb) {
                const tcp_config& conf = shared_data_ptr_->get_tcp_config();
                conf.handlers_.cb_close_(conn_info_);
            }
            shared_data_ptr_->add_closed_chan_id(conn_info_.chan_id_);
            shared_data_ptr_ = nullptr;
            evt_state_ = EVENT_STATE::CLOSED;
            return 0;
        }

        void tcp_event::remove_event(bool is_close_fd)
        {
            evt_service_ptr_->remove_event(shared_from_this());
            clear_fds_and_evts();
            STABLE_INFRA_IF_TRUE_RETURN(! is_close_fd);
            conn_info_.fd_.close();
        }

        int32_t tcp_event::try_connect(bool is_first_try)
        {
            if (!is_first_try) {
                if (conn_info_.fd_.is_valid()) {
                    this->remove_event();
                }
                auto tmp_fd = get_tcp_socket_fd();
                conn_info_.fd_ = fd_t{ tmp_fd, FD_TYPE::TCP_FD };
            }
            STABLE_INFRA_CHECK_SUC(conn_info_.fd_.is_valid(), -1);
            socklen_t len = sizeof(sockaddr_in);
            auto ret_conn = ::connect(conn_info_.fd_.get_fd(),
                                      (struct sockaddr*)&conn_info_.addr_, len);
            if (ret_conn == 0) {
                tcp_state_ = TCP_STATE::CONNECTED;
            } else {
                if (errno != EINPROGRESS) {
                    conn_info_.fd_.close();
                    return -1;
                }
                tcp_state_ = TCP_STATE::INPROGRESS;
            }
            uint16_t evt = EV_WRITE;
            auto tp = std::make_tuple(conn_info_.fd_, evt);
            fds_evts_.emplace_back(tp);
            evt_service_ptr_->add_event(shared_from_this());
            return 0;
        }

        void tcp_event::handle_read_ready(fd_t fd)
        {
            auto cb = std::bind(&tcp_event::on_read_tcp_msg,
                                     std::dynamic_pointer_cast<tcp_event>(shared_from_this()),
                                     std::placeholders::_1, std::placeholders::_2,
                                     std::placeholders::_3);
            auto ret = evt_service_ptr_->submit_async_read(fd, buf_id_, cb);
            STABLE_INFRA_ASSERT(ret == 0);
        }

        void tcp_event::on_read_tcp_msg(int32_t ret, const ::iovec* iov_ptr, uint32_t iov_cnt)
        {
            const tcp_config& conf = shared_data_ptr_->get_tcp_config();
            if (STABLE_INFRA_LIKELY(ret > 0)) {
                int32_t len = ret;
                ::iovec iov{};
                for (uint32_t i = 0; len > 0; ++i) {
                    const ::iovec* cur_iov = iov_ptr + i;
                    iov.iov_base = cur_iov->iov_base;
                    iov.iov_len = ((uint32_t)len > cur_iov->iov_len) ? cur_iov->iov_len : len;
                    len -= cur_iov->iov_len;
                    conf.handlers_.cb_data_(conn_info_, iov);
                }
            } else if (ret == 0) {
                conf.handlers_.cb_close_(conn_info_);
            } else {
                if (conf.handlers_.cb_error_ != nullptr) {
                    conf.handlers_.cb_error_(conn_info_, ret);
                }
                this->close();
            }
        }

        void tcp_event::handle_write_ready(fd_t fd)
        {
            check_and_process_tcp_connected();
        }

        void tcp_event::handle_connect_timeout()
        {
            STABLE_INFRA_IF_TRUE_RETURN(tcp_state_ == TCP_STATE::CONNECTED);
            auto ret = try_connect();
            STABLE_INFRA_IF_TRUE_RETURN(ret != 0 || tcp_state_ != TCP_STATE::CONNECTED);
            check_and_process_tcp_connected(); // connection is established
        }

        void tcp_event::on_timer()
        {
            ++cur_timer_trigger_times_;
            STABLE_INFRA_IF_TRUE_RETURN(cur_timer_trigger_times_ < 10);
            handle_connect_timeout();
        }

        void tcp_event::create_timer_for_reconnect()
        {
            cur_timer_trigger_times_ = 0;
            shared_data_ptr_->reg_timer_req(std::dynamic_pointer_cast<tcp_event>(shared_from_this()));
        }

        void tcp_event::close_reconnect_timer()
        {
            cur_timer_trigger_times_ = -1;
            shared_data_ptr_->remove_timer_req(std::dynamic_pointer_cast<tcp_event>(shared_from_this()));
        }

        void tcp_event::check_and_process_tcp_connected()
        {
            bool is_tcp_connected = check_tcp_connect();
            if (! is_tcp_connected) {
                this->remove_event();
                STABLE_INFRA_IF_TRUE_RETURN(is_timer_started());
                create_timer_for_reconnect();
                return;
            }
            auto ret = set_keep_alive(conn_info_.fd_, shared_data_ptr_->get_tcp_keep_alive_config());
            STABLE_INFRA_ASSERT(ret == 0);
            tcp_state_ = TCP_STATE::CONNECTED;
            close_reconnect_timer();
            this->remove_event(false);
            uint16_t evt = EV_READ;
            auto tp = std::make_tuple(conn_info_.fd_, evt);
            fds_evts_.emplace_back(tp);
            evt_service_ptr_->add_event(shared_from_this());
        }
        
        void tcp_event::on_started(bool is_success)
        {
            STABLE_INFRA_ASSERT(is_success);
            if (tcp_state_ == TCP_STATE::CONNECTED && evt_state_ != EVENT_STATE::DEF) {
                // call user function
                const auto& conf = shared_data_ptr_->get_tcp_config();
                conf.handlers_.cb_conn_(conn_info_);
            } else {
                ezio::event::event_base::on_started(is_success);
            }
        }

        bool tcp_event::is_tcp_connect()
        {
            struct tcp_info info;
            socklen_t len = sizeof(info);
            int32_t ret = ::getsockopt(conn_info_.fd_, IPPROTO_TCP, TCP_INFO, &info, &len);
            STABLE_INFRA_IF_TRUE_RETURN_CODE(ret != 0 || ! (info.tcpi_state == TCP_ESTABLISHED), false);
            return true;
        }

        bool tcp_event::check_tcp_connect()
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(! is_tcp_connect(), false);
            // check is self connected
            struct sockaddr_in local_addr, peer_addr;
            socklen_t addr_len = sizeof(sockaddr_in);
            bzero(&local_addr, addr_len);
            bzero(&peer_addr, addr_len);
            auto ret = ::getsockname(conn_info_.fd_, (sockaddr*)(&local_addr), &addr_len);
            STABLE_INFRA_CHECK_SUC(ret == 0, false);
            ret = ::getpeername(conn_info_.fd_, (sockaddr*)(&peer_addr), &addr_len);
            STABLE_INFRA_CHECK_SUC(ret == 0, false);
            STABLE_INFRA_CHECK_SUC(!(local_addr.sin_port == peer_addr.sin_port && local_addr.sin_addr.s_addr == peer_addr.sin_addr.s_addr), false);
            return true;
        }

        tcp_socket::tcp_socket()
        {
        }

        tcp_socket::~tcp_socket()
        {
        }

        int32_t tcp_socket::make_listen_fd()
        {
            STABLE_INFRA_CHECK_SUC(fd_.is_valid(), -1);
            int32_t fd = fd_.get_fd();
            int32_t ret = ezio::util::util_make_listen_socket_reuseable(fd);
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            ret = ezio::util::util_make_listen_socket_reuseable_port(fd);
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            const auto len = sizeof(sockaddr);
            ret = ::bind(fd, (struct sockaddr*)&sock_addr_ , len);
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            ret = ::listen(fd, MAX_WAIT_CNT);
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            return 0;
        }

        void tcp_socket::handle_read_ready(fd_t fd)
        {
            auto tcp_mgr_ptr = std::dynamic_pointer_cast<tcp_socket>(shared_from_this());
            auto cb = std::bind(&tcp_socket::on_accept, tcp_mgr_ptr, std::placeholders::_1, std::placeholders::_2);
            auto ret = evt_service_ptr_->submit_async_accept(fd, &sock_info_buffer_, cb);
            STABLE_INFRA_ASSERT(ret == 0);
        }

        chan_t tcp_socket::get_new_chan_id()
        {
            return ++chan_id_;
        }

        void tcp_socket::on_accept(int32_t ret, const sock_info& info)
        {
            // if ! is_enabled_, listen fd has been closed because fd counts has reached max count.
            STABLE_INFRA_IF_TRUE_RETURN(! is_enabled_ || ret < 0);
            auto tcp_chan_ptr = std::make_shared<tcp_event>();
            auto new_chan_id = get_new_chan_id();
            connection_info conn_info(info.sock_addr_, info.fd_, new_chan_id);
            auto ret_open = tcp_chan_ptr->open(evt_service_ptr_, conn_info, shared_data_ptr_);
            if (STABLE_INFRA_UNLIKELY(ret_open != 0)) {
                auto fd_ptr = const_cast<ezio::event::fd_t*>(&info.fd_);
                fd_ptr->close();
                return;
            }
            auto ret_insert = chan_id_to_connections_.emplace(std::make_pair(conn_info.chan_id_, std::move(tcp_chan_ptr)));
            STABLE_INFRA_ASSERT(ret_insert.second);
            ++connect_cnt_;
            STABLE_INFRA_IF_TRUE_RETURN(connect_cnt_ < shared_data_ptr_->get_tcp_config().max_connect_cnt_);
            is_enabled_ = false;
            // close listen fd because of reaching max connect count
            this->remove_event();
        }

        void tcp_socket::remove_event()
        {
            evt_service_ptr_->remove_event(shared_from_this());
            clear_fds_and_evts();
            fd_.close();
        }

        int32_t tcp_socket::open_listener(const tcp_config& conf)
        {
            auto ret = set_keep_alive(fd_.get_fd(), conf.keep_alive_conf_);
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            ret = make_listen_fd();
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            uint16_t evt = EV_READ;
            auto tp = std::make_tuple(fd_, evt);
            fds_evts_.emplace_back(tp);
            // start to monitor this event
            evt_service_ptr_->add_event(shared_from_this());
            return 0;
        }

        int32_t tcp_socket::open_connector()
        {
            auto connect_chan_id = get_new_chan_id();
            auto tcp_chan_ptr = std::make_shared<tcp_event>();
            connection_info conn_info(sock_addr_, fd_, connect_chan_id);
            auto ret_open = tcp_chan_ptr->open(evt_service_ptr_, conn_info, shared_data_ptr_);
            STABLE_INFRA_CHECK_SUC(ret_open == 0, -1);
            auto ret_insert = chan_id_to_connections_.emplace(std::make_pair(connect_chan_id, std::move(tcp_chan_ptr)));
            STABLE_INFRA_ASSERT(ret_insert.second);
            return 0;
        }

        int32_t tcp_socket::open(event_service* evt_service_ptr, const tcp_config& conf)
        {
            STABLE_INFRA_CHECK_SUC(evt_service_ptr != nullptr && evt_service_ptr_ == nullptr, -1);
            STABLE_INFRA_CHECK_SUC(conf.max_connect_cnt_ > 0, -1);
            STABLE_INFRA_CHECK_SUC(conf.handlers_.cb_conn_ != nullptr
                                   && conf.handlers_.cb_close_ != nullptr
                                   && conf.handlers_.cb_data_ != nullptr, -1);
            set_evt_service_ptr(evt_service_ptr);
            shared_data_ptr_ = std::make_shared<tcp_shared_data>(conf, evt_service_ptr);
            auto addr_port = ezio::util::split(conf.addr_, ":");
            STABLE_INFRA_CHECK_SUC(addr_port.size() == 2, -1);
            const char* ip = addr_port[0].c_str();
            uint16_t port = atoi(addr_port[1].c_str());
            struct sockaddr_in* sock_addr_ptr = reinterpret_cast<struct sockaddr_in*>(&sock_addr_);
            sock_addr_ptr->sin_family = AF_INET;
            sock_addr_ptr->sin_addr.s_addr = inet_addr(ip);
            sock_addr_ptr->sin_port = htons(port);
            sock_addr_.ss_family = AF_INET;

            auto tmp_fd = get_tcp_socket_fd();
            fd_ = fd_t{ tmp_fd, FD_TYPE::TCP_FD };
            STABLE_INFRA_CHECK_SUC(fd_.is_valid(), -1);
            int32_t ret = -1;
            if (conf.mode_ == TCP_CONNECT_MODE::LISTEN) {
                ret = open_listener(conf);
            } else {
                ret = open_connector();
            }
            if (ret != 0) {
                fd_.close();
                return -1;
            }

            auto cb = std::bind(&tcp_socket::on_timer,
                                std::dynamic_pointer_cast<tcp_socket>(shared_from_this()));
            // 10ms timer
            timer_id_ = evt_service_ptr_->create_timer(0, 100000000, std::move(cb));
            STABLE_INFRA_ASSERT(timer_id_ > 0);
            return 0;
        }

        int32_t tcp_socket::close()
        {
            ezio::event::event_base::close(); // parent clear
            if (timer_id_ != -1) {
                auto ret = evt_service_ptr_->close_timer(timer_id_);
                STABLE_INFRA_ASSERT(ret == 0);
                timer_id_ = -1;
            }
            for (auto& kv : chan_id_to_connections_) {
                kv.second->close();
            }
            chan_id_to_connections_.clear();
            is_enabled_ = false;
            return 0;
        }

        int32_t tcp_socket::close_tcp_channel(chan_t chan_id)
        {
            auto iter = chan_id_to_connections_.find(chan_id);
            STABLE_INFRA_CHECK_SUC(iter != chan_id_to_connections_.end(), -1);
            iter->second->close();
            return 0;
        }

        int32_t tcp_socket::on_timer()
        {
            const auto& config = shared_data_ptr_->get_tcp_config();
            if (config.mode_ == TCP_CONNECT_MODE::LISTEN) {
                auto& closed_chan_ids = shared_data_ptr_->get_closed_chan_ids();
                // remove closed tcp channel
                for (const auto chan_id : closed_chan_ids) {
                    chan_id_to_connections_.erase(chan_id);
                }
                closed_chan_ids.clear();
                connect_cnt_ = chan_id_to_connections_.size();
                if (is_reopen_listener()) {
                    auto ret = reopen_listener();
                    STABLE_INFRA_ASSERT(ret == 0);
                }
            }
            const auto& req_timer_set = shared_data_ptr_->get_req_timer_set();
            for (const auto& tcp_ptr : req_timer_set) {
                tcp_ptr->on_timer();
            }
            return 0;
        }
        
        int32_t tcp_socket::reopen_listener()
        {
            this->remove_event();
            auto tmp_fd = get_tcp_socket_fd();
            fd_ = ezio::event::fd_t{ tmp_fd, FD_TYPE::TCP_FD };
            STABLE_INFRA_CHECK_SUC(fd_.is_valid(), -1);
            auto ret = make_listen_fd();
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            uint16_t evt = EV_READ;
            auto tp = std::make_tuple(fd_, evt);
            fds_evts_.emplace_back(tp);
            evt_service_ptr_->add_event(shared_from_this());
            is_enabled_ = true;
            return 0;
        }

        int32_t tcp_socket::async_send(const connection_info& conn, ::iovec* data_iov, uint32_t iov_cnt, const std::function<void(int32_t)>& cb)
        {
            STABLE_INFRA_CHECK_SUC(data_iov != nullptr && iov_cnt != 0, -1);
            return evt_service_ptr_->submit_async_write(conn.fd_, data_iov, iov_cnt, cb);
        }
                
        int32_t tcp_socket::cancel_async_send(const connection_info& conn, const std::function<void(int32_t)>& cb)
        {
            return evt_service_ptr_->cancel_async_write(conn.fd_, cb);
        }

        const std::string connection_info::get_ip_addr() const
        {
            struct sockaddr_in *addr = (struct sockaddr_in *)&addr_;
            char ip_str[INET_ADDRSTRLEN];
            auto ret = inet_ntop(AF_INET, &addr->sin_addr, ip_str, INET_ADDRSTRLEN);
            if (ret != nullptr) {
                return ret;
            }
            return "";
        }
            
        const uint32_t connection_info::get_port() const
        {
            struct sockaddr_in *addr = (struct sockaddr_in *)&addr_;
            return ntohs(addr->sin_port);
        }
    }
}
