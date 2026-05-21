#ifndef __ERRORS_H__
#define __ERRORS_H__

#include "../Define.hpp"

namespace CD
{
enum class Errors : CD::byte
{
  Success = 0x00,
  Failture = 0x01
};
}

#endif  // __ERRORS_H__