#include <memory_resource>
#include <iostream>
#include <memory>

int main() {
    char buffer[1024];
    std::pmr::monotonic_buffer_resource pool{buffer, sizeof(buffer)};
    std::pmr::polymorphic_allocator<int> alloc{&pool};
    auto ptr = std::allocate_shared<int>(alloc, 42);
    std::cout << *ptr << std::endl;
}
