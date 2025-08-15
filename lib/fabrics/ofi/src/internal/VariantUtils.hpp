#pragma once

// Allows us to use the visitor pattern with lambdas
template<class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};
