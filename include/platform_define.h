/****************************************************************************************
 * @file platform_define.h
 * @brief platform definition
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once

// platform define
#if defined(_WIN64)
#define _OS_WINDOW_64
#elif defined(_WIN32)
#define _OS_WINDOW_32
#elif defined(__linux__)
#define _OS_LINUX
#else
#error "unknow compiler"
#endif

#if defined(_OS_LINUX)
#define EPOLL_IS_SUPPORTED
#define INOTIFY_IS_SUPPORTED
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
#define IOURING_IS_SUPPORTED
#endif
#elif defined(_OS_WINDOW_64) || defined(_OS_WINDOW_32)
#define IOCP_IS_SUPPORTED
#else
#error "unknow poller"
#endif
