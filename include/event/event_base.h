/****************************************************************************************
 * @file event_base.h
 * @brief base class for events
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>
#include <unordered_map>
#include "../common/internal_type_def.h"

namespace ezio {
    namespace event {

        enum class EVENT_STATE : uint16_t
        {
            DEF = 0,
            OPENED = 1,
            CLOSED = 2,
        };

        class poll_base;
        class event_service;
        class event_base : public std::enable_shared_from_this<event_base>
        {
            public:
                enum class EVENT_TYPE : uint8_t
                {
                    NORMAL = 0,
                    BUSY_POLL = 1,
                };

            public:
                typedef std::shared_ptr<event_base> pointer_t;
                event_base() {}
                virtual ~event_base() {}
                virtual EVENT_TYPE get_event_type() const {
                    return EVENT_TYPE::NORMAL;
                }
                virtual bool try_raise_priority() { return false; }
                /**< busy poll handler*/
                virtual void handle_loop_once() {}
                virtual void on_started(bool is_success) {
                    evt_state_ = (is_success) ? EVENT_STATE::OPENED : evt_state_;
                }
                virtual void on_stopped(bool is_success) {
                    evt_state_ = (is_success) ? EVENT_STATE::CLOSED : evt_state_;
                }
                //virtual void set_weight_thredhold(uint32_t thredhold) {}
                virtual void clear_weight() {}
                inline void set_read_callback(const callback_t& read_cb) {
                    read_cb_ = read_cb;
                }
                inline void set_cancel_read_callback(const callback_t& cancel_read_cb) {
                    cancel_read_cb_ = cancel_read_cb;
                }
                inline void set_write_callback(const callback_t& write_cb) {
                    write_cb_ = write_cb;
                }
                inline void set_cancel_write_callback(const callback_t& cancel_write_cb) {
                    cancel_write_cb_ = cancel_write_cb;
                }
                inline const callback_t& get_read_callback() const {
                    return read_cb_;
                }
                inline const callback_t& get_cancel_read_callback() const {
                    return cancel_read_cb_;
                }
                inline const callback_t& get_write_callback() const {
                    return write_cb_;
                }
                inline const callback_t& get_cancel_write_callback() const {
                    return cancel_write_cb_;
                }
                inline void clear_read_callback() {
                    read_cb_ = nullptr;
                }
                inline void clear_cancel_read_callback() {
                    cancel_read_cb_ = nullptr;
                }
                inline void clear_write_callback() {
                    write_cb_ = nullptr;
                }
                inline void clear_cancel_write_callback() {
                    cancel_write_cb_ = nullptr;
                }
                //inline void set_close_callback(callback cb) { close_cb_ = cb; };
                //inline void set_error_callback(callback cb) { error_cb_ = cb; };
                //inline callback get_close_callback() const { return close_cb_; };
                //inline callback get_error_callback() const { return error_cb_; };
                inline const std::vector<std::tuple<fd_t, uint16_t>>& get_fds_and_evts() const {
                    return fds_evts_;
                }
                inline void clear_fds_and_evts() {
                    fds_evts_.clear();
                }
           // protected:
           //     inline void clear_close_callback() { close_cb_ = nullptr; }
           //     inline void clear_error_callback() { error_cb_ = nullptr; }
                void close();
                void handle_event_ready(int32_t ret, fd_t fd, bool is_read);
                inline void set_evt_service_ptr(event_service* ptr) {
                    if (ptr != nullptr) {
                        evt_service_ptr_ = ptr;
                    }
                }
            protected:
                virtual void handle_read_ready(fd_t fd) {}
                virtual void handle_write_ready(fd_t fd) {}
            protected:
                std::vector<std::tuple<fd_t, uint16_t>> fds_evts_;
                callback_t read_cb_{nullptr};
                callback_t cancel_read_cb_{nullptr};
                callback_t write_cb_{nullptr};
                callback_t cancel_write_cb_{nullptr};
           //     callback close_cb_{nullptr};
           //     callback error_cb_{nullptr};
                event_service* evt_service_ptr_{ nullptr };
                EVENT_STATE evt_state_{ EVENT_STATE::DEF };
        };
    }
}
