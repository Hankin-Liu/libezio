/****************************************************************************************
 * @file udp_socket.h
 * @brief udp socket logic
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <netinet/in.h>
#include <string>
#include "../event/event_base.h"

namespace ezio {
    namespace socket {
        using udp_data_callback = std::function<void(const ::iovec&, const struct sockaddr_storage*)>;
        using udp_error_callback = std::function<void(int32_t)>;
        struct handler
        {
            udp_data_callback cb_data_{ nullptr };
            udp_error_callback cb_error_{ nullptr };
        };

        struct udp_config
        {
            std::string addr_{""};
            std::string peer_addr_{""};
            uint32_t block_size_{ 4096 };
            uint32_t block_count_{ 256 };
            bool need_read_detail_{ false }; // control info, address info
            handler handlers_{};
            void reg_handler(const udp_data_callback& cb_data,
                             const udp_error_callback& cb_error)
            {
                handlers_.cb_data_ = cb_data;
                handlers_.cb_error_ = cb_error;
            }
        };

        class udp_socket : public ezio::event::event_base
        {
            constexpr udp_socket(udp_socket&) = delete;
            udp_socket& operator=(udp_socket&) = delete;
            public:
                udp_socket();
                virtual ~udp_socket();
            public:
                int32_t open(ezio::event::event_service* evt_service_ptr, const udp_config& conf);
                int32_t close();
                int32_t async_send(::iovec* data_iov, uint32_t iov_cnt, const std::function<void(int32_t)>& cb = nullptr, struct sockaddr_storage* addr = nullptr);
                int32_t cancel_async_send(const std::function<void(int32_t)>& cb);
                int32_t connect(const struct sockaddr_storage* addr);
            private:
                void on_read_udp_msg(int32_t ret, const ::iovec* iov_ptr, uint32_t iov_cnt, void* mmsghdrs);
                virtual void handle_read_ready(ezio::event::fd_t fd) override;
            private:
                ezio::event::fd_t fd_{};
                udp_config config_{};
                struct sockaddr_storage* sock_addr_ptr_{ nullptr };
                struct sockaddr_storage* peer_sock_addr_ptr_{ nullptr };
                int32_t buf_id_{ -1 };
        };
    }
}
