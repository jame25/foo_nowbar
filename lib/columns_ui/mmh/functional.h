#pragma once

namespace mmh {

struct GUIDHasher {
    std::size_t operator()(const GUID& value) const
    {
        const auto hashes = {std::hash<unsigned long>()(value.Data1), std::hash<unsigned short>()(value.Data2),
            std::hash<unsigned short>()(value.Data3),
            std::hash<uint64_t>()(*reinterpret_cast<const uint64_t*>(&value.Data4[0]))};
        return std::accumulate(hashes.begin(), hashes.end(), size_t{}, std::bit_xor<>());
    }
};

} // namespace mmh
