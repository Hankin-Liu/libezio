/**
 * @file err_no.h
 * @brief errno number
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 */
#pragma once
#include <stdint.h>

extern thread_local int32_t error_no;
enum ERROR_NO
{
    READER_EVENT_NOT_RELEASE = 1,
};
