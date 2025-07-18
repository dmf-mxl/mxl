// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#include "Time.hpp"
#include <ctime>

bool operator==(timespec const& lhs, timespec const& rhs)
{
    return (lhs.tv_sec == rhs.tv_sec) && (lhs.tv_nsec == rhs.tv_nsec);
}

bool operator!=(timespec const& lhs, timespec const& rhs)
{
    return !(lhs == rhs);
}

bool operator<(timespec const& lhs, timespec const& rhs)
{
    if (lhs.tv_sec < rhs.tv_sec)
    {
        return true;
    }
    if (lhs.tv_sec > rhs.tv_sec)
    {
        return false;
    }
    return lhs.tv_nsec < rhs.tv_nsec;
}

bool operator<=(timespec const& lhs, timespec const& rhs)
{
    return (lhs < rhs) || (lhs == rhs);
}

bool operator>(timespec const& lhs, timespec const& rhs)
{
    return !(lhs <= rhs);
}

bool operator>=(timespec const& lhs, timespec const& rhs)
{
    return !(lhs < rhs);
}