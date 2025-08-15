#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <rdma/fi_eq.h>

namespace mxl::lib::fabrics::ofi
{
    class CompletionQueue;

    class Completion
    {
    public:
        class Data
        {
        public:
            [[nodiscard]]
            std::optional<uint64_t> data() const noexcept;
            [[nodiscard]]
            ::fid_ep* fid() const noexcept;

            [[nodiscard]]
            bool isRemoteWrite() const noexcept;
            [[nodiscard]]
            bool isLocalWrite() const noexcept;

        private:
            explicit Data(::fi_cq_data_entry const& raw);

            friend class CompletionQueue;

            ::fi_cq_data_entry _raw;
        };

        class Error
        {
        public:
            [[nodiscard]]
            std::string toString() const;

            [[nodiscard]]
            ::fid_ep* fid() const noexcept;

        private:
            explicit Error(::fi_cq_err_entry const& raw, std::shared_ptr<CompletionQueue> queue);

            friend class CompletionQueue;

            ::fi_cq_err_entry _raw;
            std::shared_ptr<CompletionQueue> _cq;
        };

        explicit Completion(Data entry);
        explicit Completion(Error entry);

        Data data();
        Error err();

        [[nodiscard]]
        std::optional<Data> tryData() const noexcept;
        [[nodiscard]]
        std::optional<Error> tryErr() const noexcept;

        [[nodiscard]]
        bool isDataEntry() const noexcept;
        [[nodiscard]]
        bool isErrEntry() const noexcept;

        [[nodiscard]]
        ::fid_ep* fid() const noexcept;

    private:
        friend class CompletionQueue;

        using Inner = std::variant<Data, Error>;

        Inner _inner;
    };
}
