/**
 * @file macros_func.h
 * @brief common macros functions
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 */
#pragma once
#include <cstdio>

#if __GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ > 91)
#define STABLE_INFRA_LIKELY(x) __builtin_expect(!!(x), 1)
#define STABLE_INFRA_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define STABLE_INFRA_LIKELY(x) (x)
#define STABLE_INFRA_UNLIKELY(x) (x)
#endif

#define STABLE_INFRA_CHECK_SUC(expr, ret) \
    if ((STABLE_INFRA_UNLIKELY(!(expr)))) \
    {\
        return (ret); \
    }

#define STABLE_INFRA_ASSERT(expr) \
    if ((STABLE_INFRA_UNLIKELY(!(expr)))) \
    {\
        printf("%s:%d|%s\n", __FILE__, __LINE__, __FUNCTION__);\
        std::exit(-1);\
    }

#define STABLE_INFRA_IF_TRUE_RETURN_CODE(expr, retcode) \
    if ((expr)) \
    {\
        return retcode; \
    }

#define STABLE_INFRA_IF_TRUE_RETURN(expr) \
    if ((expr)) \
    {\
        return; \
    }

#define STABLE_INFRA_IF_TRUE_CONTINUE(expr) \
    if ((expr)) \
    {\
        continue; \
    }

#define STABLE_INFRA_IF_TRUE_BREAK(expr) \
    if ((expr)) \
    {\
        break; \
    }

#define STABLE_INFRA_DELETE_OBJ(p) if (p != nullptr) {delete p; p = nullptr;}

#define STABLE_INFRA_DELETE_ADDR(addr) \
    if (nullptr != addr) \
    {\
        delete [] addr; \
        addr = nullptr; \
    }

#define STABLE_INFRA_MAKE_UNIQUE(class_name, ...) \
    (std::unique_ptr<class_name>(new class_name(__VA_ARGS__)))
