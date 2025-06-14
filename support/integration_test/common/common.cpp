#include <stdlib.h>
#include <time.h>
#include "common.h"

char* rand_str(char *str, const int len)
{
    if (len == 0) {
        str = nullptr;
        return nullptr;
    }
    static int t = 0;
    if (t++ == 0) {
        srand(time(NULL));
    }
    int i = 0;
    for (; i < len; ++i)
    {
        switch ((rand() % 3))
        {
        case 1:
            str[i] = 'A' + rand() % 26;
            break;
        case 2:
            str[i] = 'a' + rand() % 26;
            break;
        default:
            str[i] = '0' + rand() % 10;
            break;
        }
    }
    str[len - 1] = '\0';
    return str;
}
