/**
 * @file internal_type_def.h
 * @brief type definition for internal use
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 */
#pragma once
#include <stdint.h>
#include <functional>
#include "platform_define.h"
#ifdef _OS_LINUX
#include <netinet/in.h>
#endif
#include "../type_def.h"

#if defined(_OS_LINUX)
typedef uint32_t signo_t;
#endif

namespace ezio
{
namespace event
{
    enum class EVENT : uint16_t
    {
        EV_READ = 0x1,  ///< read event
#define EV_READ (uint16_t)(ezio::event::EVENT::EV_READ)
        EV_WRITE = 0X2, ///< write event
#define EV_WRITE (uint16_t)(ezio::event::EVENT::EV_WRITE)
        EV_CLOSE = 0x4, ///< close event
#define EV_CLOSE (uint16_t)(ezio::event::EVENT::EV_CLOSE)
        EV_ERR = 0X8,   ///< error event
#define EV_ERR (uint16_t)(ezio::event::EVENT::EV_ERR)
        EV_ET = 0x10,   ///< ET mode
#define EV_ET (uint16_t)(ezio::event::EVENT::EV_ET)
    };

    using callback_t = std::function<void(int32_t ret)>;
    using accept_callback_t =  std::function<void(int32_t, const sock_info&)>;
    using pending_func = std::function<void(void)>;
}
}
