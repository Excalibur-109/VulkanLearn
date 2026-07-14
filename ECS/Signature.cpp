#include "Signature.hpp"

namespace ecs {

Signature::Signature(std::initializer_list<ComponentTypeId> componentTypes) {
    for (const ComponentTypeId componentType : componentTypes) {
        Set(componentType);
    }
}

void Signature::Set(ComponentTypeId componentType) {
    const std::size_t wordIndex = componentType / BITS_PER_WORD;
    if (words_.size() <= wordIndex) {
        words_.resize(wordIndex + 1U, 0);
    }
    words_[wordIndex] |= u64{1} << (componentType % BITS_PER_WORD);
}

void Signature::Reset(ComponentTypeId componentType) {
    const std::size_t wordIndex = componentType / BITS_PER_WORD;
    if (wordIndex >= words_.size()) {
        return;
    }
    words_[wordIndex] &= ~(u64{1} << (componentType % BITS_PER_WORD));
    RemoveTrailingZeroWords();
}

bool Signature::Test(ComponentTypeId componentType) const noexcept {
    const std::size_t wordIndex = componentType / BITS_PER_WORD;
    return wordIndex < words_.size() &&
           (words_[wordIndex] & (u64{1} << (componentType % BITS_PER_WORD))) != 0;
}

bool Signature::ContainsAll(const Signature& required) const noexcept {
    for (std::size_t index = 0; index < required.words_.size(); ++index) {
        const u64 ownWord = index < words_.size() ? words_[index] : 0;
        if ((ownWord & required.words_[index]) != required.words_[index]) {
            return false;
        }
    }
    return true;
}

bool Signature::Intersects(const Signature& other) const noexcept {
    const std::size_t commonWordCount = std::min(words_.size(), other.words_.size());
    for (std::size_t index = 0; index < commonWordCount; ++index) {
        if ((words_[index] & other.words_[index]) != 0) {
            return true;
        }
    }
    return false;
}

bool Signature::Empty() const noexcept {
    return words_.empty();
}

std::size_t Signature::Count() const noexcept {
    std::size_t count = 0;
    for (const u64 word : words_) {
        count += std::popcount(word);
    }
    return count;
}

std::vector<ComponentTypeId> Signature::ComponentTypes() const {
    std::vector<ComponentTypeId> result;
    result.reserve(Count());

    for (std::size_t wordIndex = 0; wordIndex < words_.size(); ++wordIndex) {
        u64 remainingBits = words_[wordIndex];
        while (remainingBits != 0) {
            const u32 bit = static_cast<u32>(std::countr_zero(remainingBits));
            result.push_back(static_cast<ComponentTypeId>(wordIndex * BITS_PER_WORD + bit));
            remainingBits &= remainingBits - 1U;
        }
    }
    return result;
}

void Signature::RemoveTrailingZeroWords() noexcept {
    while (!words_.empty() && words_.back() == 0) {
        words_.pop_back();
    }
}

std::size_t SignatureHash::operator()(const Signature& signature) const noexcept {
    // boost::hash_combine 的常用混合方式，逐个纳入所有非尾零 word。
    std::size_t seed = signature.Words().size();
    for (const u64 word : signature.Words()) {
        const std::size_t value = std::hash<u64>{}(word);
        seed ^= value + static_cast<std::size_t>(0x9e3779b9U) + (seed << 6U) + (seed >> 2U);
    }
    return seed;
}

} // namespace ecs

