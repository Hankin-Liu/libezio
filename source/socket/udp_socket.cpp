/****************************************************************************************
  > File Name: udp_socket.cpp
  > Author: Hua Jun Liu
  > Mail: wojiaoliuhuajun.com 
  > Note: Use of this source code is governed by The GNU Affero General Public License
          which can be found in the LICENSE file
 ***************************************************************************************/
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <cstring>
#include "../../include/socket/udp_socket.h"
#include "../../include/util/util.h"
#include "../../include/type_def.h"
#include "../../include/event_service.h"

namespace ezio{
    namespace socket {
        static int32_t get_udp_socket_fd()
        {
            auto fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
            STABLE_INFRA_CHECK_SUC(fd > 0, INVALID_FD);
            return fd;
        }

        udp_socket::udp_socket()
        {
        }

        udp_socket::~udp_socket()
        {
            if (sock_addr_ptr_ != nullptr) {
                delete sock_addr_ptr_;
            }
            if (peer_sock_addr_ptr_ != nullptr) {
                delete peer_sock_addr_ptr_;
            }
        }

        int32_t udp_socket::open(ezio::event::event_service* evt_service_ptr, const udp_config& conf)
        {
            STABLE_INFRA_CHECK_SUC(evt_service_ptr != nullptr && evt_service_ptr_ == nullptr, -1);
            set_evt_service_ptr(evt_service_ptr);
            config_ = conf;
            if (! config_.addr_.empty()) {
                auto addr_port = ezio::util::split(config_.addr_, ":");
                STABLE_INFRA_CHECK_SUC(addr_port.size() == 2, -1);
                const char* ip = addr_port[0].c_str();
                uint16_t port = atoi(addr_port[1].c_str());
                sock_addr_ptr_ = new sockaddr_storage{};
                auto tmp_ptr = (struct sockaddr_in*)sock_addr_ptr_;
                tmp_ptr->sin_family = AF_INET;
                tmp_ptr->sin_addr.s_addr = inet_addr(ip);
                tmp_ptr->sin_port = htons(port);
            }

            if (! config_.peer_addr_.empty()) {
                auto peer_addr_port = ezio::util::split(config_.peer_addr_, ":");
                STABLE_INFRA_CHECK_SUC(peer_addr_port.size() == 2, -1);
                const char* peer_ip = peer_addr_port[0].c_str();
                uint16_t peer_port = atoi(peer_addr_port[1].c_str());
                peer_sock_addr_ptr_ = new sockaddr_storage{};
                auto tmp_ptr = (struct sockaddr_in*)peer_sock_addr_ptr_;
                tmp_ptr->sin_family = AF_INET;
                tmp_ptr->sin_addr.s_addr = inet_addr(peer_ip);
                tmp_ptr->sin_port = htons(peer_port);
            }
            STABLE_INFRA_CHECK_SUC(sock_addr_ptr_ != nullptr || peer_sock_addr_ptr_ != nullptr, -1);
                
            buf_id_ = evt_service_ptr_->register_ring_buffer(config_.block_size_, config_.block_count_);
            STABLE_INFRA_CHECK_SUC(0 <= buf_id_, -1);

            auto tmp_fd = get_udp_socket_fd();
            fd_ = ezio::event::fd_t{ tmp_fd, ezio::event::FD_TYPE::UDP_FD };
            STABLE_INFRA_CHECK_SUC(fd_.is_valid(), -1);
            int32_t ret = -1;
            do {
                if (sock_addr_ptr_ != nullptr) {
                    ret = ezio::util::util_make_listen_socket_reuseable(fd_.get_fd());
                    STABLE_INFRA_IF_TRUE_BREAK(ret != 0);
                    ret = ezio::util::util_make_listen_socket_reuseable_port(fd_.get_fd());
                    STABLE_INFRA_IF_TRUE_BREAK(ret != 0);
                    ret = ::bind(fd_.get_fd(), (struct sockaddr*)sock_addr_ptr_, sizeof(struct sockaddr));
                    STABLE_INFRA_IF_TRUE_BREAK(ret != 0);
                }
                if (peer_sock_addr_ptr_ != nullptr) {
                    ret = ::connect(fd_.get_fd(), (struct sockaddr*)peer_sock_addr_ptr_, sizeof(sockaddr_in));
                    STABLE_INFRA_IF_TRUE_BREAK(ret != 0);
                }
            } while(false);
            if (ret != 0) {
                fd_.close();
                return -1;
            }
            uint16_t evt = EV_READ | EV_WRITE;
            auto tp = std::make_tuple(fd_, evt);
            fds_evts_.emplace_back(tp);

            // start to monitor this event
            evt_service_ptr_->add_event(shared_from_this());
            return 0;
        }

        void udp_socket::handle_read_ready(ezio::event::fd_t fd)
        {
            auto cb = std::bind(&udp_socket::on_read_udp_msg,
                                     std::dynamic_pointer_cast<udp_socket>(shared_from_this()),
                                     std::placeholders::_1, std::placeholders::_2,
                                     std::placeholders::_3, std::placeholders::_4);
            ezio::event::read_options opt{};
            if (config_.need_read_detail_) {
                opt.set_need_socket_detail(true);
            }
            auto ret = evt_service_ptr_->submit_async_read(fd, buf_id_, cb, &opt);
            STABLE_INFRA_ASSERT(ret == 0);
        }
        
        int32_t udp_socket::cancel_async_send(const std::function<void(int32_t)>& cb)
        {
            return evt_service_ptr_->cancel_async_write(fd_, cb);
        }

        int32_t udp_socket::async_send(::iovec* data_iov, uint32_t iov_cnt, const std::function<void(int32_t)>& cb, sockaddr_storage* addr)
        {
            STABLE_INFRA_CHECK_SUC(data_iov != nullptr && iov_cnt != 0, -1);
            ezio::event::write_options opt{};
            if (addr == nullptr) {
                STABLE_INFRA_CHECK_SUC(peer_sock_addr_ptr_ != nullptr, -1);
                opt.set_sockaddr(peer_sock_addr_ptr_);
            } else {
                opt.set_sockaddr(addr);
            }
            return evt_service_ptr_->submit_async_write(fd_, data_iov, iov_cnt, cb, &opt);
        }
                
        int32_t udp_socket::connect(const struct sockaddr_storage* addr)
        {
            STABLE_INFRA_CHECK_SUC(addr != nullptr, -1);
            auto ret = ::connect(fd_, (struct sockaddr*)addr, sizeof(sockaddr_in));
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            return 0;
        }

        void udp_socket::on_read_udp_msg(int32_t ret, const ::iovec* iov_ptr, uint32_t iov_cnt, void* m_info)
        {
            if (STABLE_INFRA_LIKELY(ret > 0)) {
                struct sockaddr_storage* sockaddr_ptr = nullptr;
                if (m_info != nullptr) {
                    sockaddr_ptr = ((ezio::event::socket_more_info*)m_info)->get_sockaddr_storage();
                }
                config_.handlers_.cb_data_(*iov_ptr, sockaddr_ptr);
            } else if (ret < 0) {
                if (config_.handlers_.cb_error_ != nullptr) {
                    config_.handlers_.cb_error_(ret);
                }
            }
        }

        int32_t udp_socket::close()
        {
            ezio::event::event_base::close(); // parent close
            evt_service_ptr_->unregister_ring_buffer(buf_id_);
            buf_id_ = -1;
            return 0;
        }
    }
}
