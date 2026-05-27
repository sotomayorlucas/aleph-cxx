module;
#include <memory_resource>

export module aleph.alloc:pmr_adapter;

export namespace aleph::alloc {

// Convenience alias for stdlib containers.
template<typename T>
using pmr_allocator = std::pmr::polymorphic_allocator<T>;

// Sanity ctor adapter: takes any of our resources and returns the same
// memory_resource* pointer typed as the stdlib base.
template<typename Resource>
inline std::pmr::memory_resource* as_resource(Resource& r) noexcept {
    return &r;
}

}  // namespace aleph::alloc
