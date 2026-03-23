#include <memory>
#include <vector>
#include <iostream>

template <typename T>
struct ArenaAlloc {
    using value_type = T;
    ArenaAlloc() = default;
    template <class U> constexpr ArenaAlloc(const ArenaAlloc<U>&) noexcept {}
    T* allocate(std::size_t n) { return static_cast<T*>(::operator new(n * sizeof(T))); }
    void deallocate(T* p, std::size_t n) noexcept { ::operator delete(p); }
};
template <class T, class U>
bool operator==(const ArenaAlloc<T>&, const ArenaAlloc<U>&) { return true; }
template <class T, class U>
bool operator!=(const ArenaAlloc<T>&, const ArenaAlloc<U>&) { return false; }

int main() {
    auto ptr = std::allocate_shared<int>(ArenaAlloc<int>{}, 42);
    std::cout << *ptr << std::endl;
}
