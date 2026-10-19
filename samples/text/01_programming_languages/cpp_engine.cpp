#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>

namespace WinPreview::Core {

template <typename T>
class RenderBuffer {
public:
    explicit RenderBuffer(size_t capacity) : m_capacity(capacity) {
        m_data.reserve(capacity);
    }

    void Push(T element) {
        if (m_data.size() < m_capacity) {
            m_data.push_back(std::move(element));
        }
    }

    [[nodiscard]] size_t Size() const noexcept { return m_data.size(); }
    [[nodiscard]] bool IsFull() const noexcept { return m_data.size() >= m_capacity; }

private:
    size_t m_capacity;
    std::vector<T> m_data;
};

} // namespace WinPreview::Core

int main() {
    using namespace WinPreview::Core;
    auto buffer = std::make_unique<RenderBuffer<float>>(1024);
    buffer->Push(60.0f);
    buffer->Push(120.0f);
    buffer->Push(144.0f);

    std::cout << "RenderBuffer initialized with " << buffer->Size() << " refresh rates." << std::endl;
    return 0;
}
