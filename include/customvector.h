#pragma once

#include <initializer_list>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <cassert>


class ReserveProxyObj {
public:
    ReserveProxyObj(size_t capacity) : capacity_(capacity) {}
    size_t GetCapacity() {
        return capacity_;
    }
private:
    size_t capacity_;
};

template <typename Type, typename Allocator = std::allocator<Type>>
class SimpleVector {
public:
    using Iterator = Type*;
    using ConstIterator = const Type*;
    using allocator_type = Allocator;
    using alloc_traits = std::allocator_traits<Allocator>;

    SimpleVector() noexcept : allocator_(Allocator()) {}

    explicit SimpleVector(size_t size) : allocator_(Allocator()) {
        create_storage(size);
        size_ = size;
        capacity_ = size;
        default_construct_elements(size);
    }

    SimpleVector(size_t size, const Type& value) : allocator_(Allocator()) {
        create_storage(size);
        size_ = size;
        capacity_ = size;
        fill_construct_elements(size, value);
    }

    SimpleVector(std::initializer_list<Type> init) : allocator_(Allocator()) {
        create_storage(init.size());
        size_ = init.size();
        capacity_ = init.size();
        copy_construct_elements(init.begin(), init.end());
    }

    SimpleVector(const SimpleVector& other) 
        : allocator_(alloc_traits::select_on_container_copy_construction(other.allocator_)) {
        create_storage(other.size_);
        size_ = other.size_;
        capacity_ = other.size_;
        copy_construct_elements(other.begin(), other.end());
    }

    SimpleVector(SimpleVector&& other) noexcept 
        : items_(other.items_), size_(other.size_), capacity_(other.capacity_), 
          allocator_(std::move(other.allocator_)) {
        other.items_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    SimpleVector(ReserveProxyObj r) : allocator_(Allocator()) {
        create_storage(r.GetCapacity());
        size_ = 0;
        capacity_ = r.GetCapacity();
    }

    explicit SimpleVector(const Allocator& alloc) noexcept : allocator_(alloc) {}

    SimpleVector(size_t size, const Allocator& alloc = Allocator()) : allocator_(alloc) {
        create_storage(size);
        size_ = size;
        capacity_ = size;
        default_construct_elements(size);
    }

    SimpleVector(size_t size, const Type& value, const Allocator& alloc) : allocator_(alloc) {
        create_storage(size);
        size_ = size;
        capacity_ = size;
        fill_construct_elements(size, value);
    }

    SimpleVector(std::initializer_list<Type> init, const Allocator& alloc) : allocator_(alloc) {
        create_storage(init.size());
        size_ = init.size();
        capacity_ = init.size();
        copy_construct_elements(init.begin(), init.end());
    }

    SimpleVector(const SimpleVector& other, const Allocator& alloc) : allocator_(alloc) {
        create_storage(other.size_);
        size_ = other.size_;
        capacity_ = other.size_;
        copy_construct_elements(other.begin(), other.end());
    }

    SimpleVector(SimpleVector&& other, const Allocator& alloc) : allocator_(alloc) {
        if (allocator_ == other.allocator_) {
            // Если аллокаторы одинаковые, можно переместить память
            items_ = other.items_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            
            other.items_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        } else {
            // Если аллокаторы разные, нужно скопировать элементы
            create_storage(other.size_);
            size_ = other.size_;
            capacity_ = other.size_;
            copy_construct_elements(other.begin(), other.end());
        }
    }

    ~SimpleVector() {
        destroy_elements();
        deallocate_storage();
    }

    SimpleVector& operator=(const SimpleVector& rhs) {
        if (this != &rhs) {
            // Проверяем, нужно ли копировать аллокатор
            if constexpr (alloc_traits::propagate_on_container_copy_assignment::value) {
                if (allocator_ != rhs.allocator_) {
                    // Если аллокаторы разные и нужно распространять, освобождаем текущую память
                    destroy_elements();
                    deallocate_storage();
                    allocator_ = rhs.allocator_;
                }
            }
            
            SimpleVector tmp(rhs);
            swap(tmp);
        }
        return *this;
    }

    SimpleVector& operator=(SimpleVector&& rhs) noexcept {
        if (this != &rhs) {
            // Проверяем, нужно ли копировать аллокатор при перемещении
            if constexpr (alloc_traits::propagate_on_container_move_assignment::value) {
                // Если нужно распространять, просто перемещаем всё
                destroy_elements();
                deallocate_storage();
                
                items_ = rhs.items_;
                size_ = rhs.size_;
                capacity_ = rhs.capacity_;
                allocator_ = std::move(rhs.allocator_);
                
                rhs.items_ = nullptr;
                rhs.size_ = 0;
                rhs.capacity_ = 0;
            } else {
                if (allocator_ == rhs.allocator_) {
                    // Аллокаторы одинаковые, можно переместить память
                    destroy_elements();
                    deallocate_storage();
                    
                    items_ = rhs.items_;
                    size_ = rhs.size_;
                    capacity_ = rhs.capacity_;
                    
                    rhs.items_ = nullptr;
                    rhs.size_ = 0;
                    rhs.capacity_ = 0;
                } else {
                    // Аллокаторы разные, нужно скопировать элементы
                    SimpleVector tmp(std::move(rhs));
                    swap(tmp);
                }
            }
        }
        return *this;
    }

    void PushBack(const Type& item) {
        if (size_ < capacity_) {
            alloc_traits::construct(allocator_, items_ + size_, item);
            ++size_;
        } else {
            size_t new_capacity = capacity_ == 0 ? 1 : capacity_ * 2;
            resize_storage(new_capacity);
            alloc_traits::construct(allocator_, items_ + size_, item);
            ++size_;
        }
    }

    void PushBack(Type&& item) {
        if (size_ < capacity_) {
            alloc_traits::construct(allocator_, items_ + size_, std::move(item));
            ++size_;
        } else {
            size_t new_capacity = capacity_ == 0 ? 1 : capacity_ * 2;
            resize_storage(new_capacity);
            alloc_traits::construct(allocator_, items_ + size_, std::move(item));
            ++size_;
        }
    }

    template <typename... Args>
    Type& EmplaceBack(Args&&... args) {
        if (size_ < capacity_) {
            alloc_traits::construct(allocator_, items_ + size_, std::forward<Args>(args)...);
            ++size_;
        } else {
            size_t new_capacity = capacity_ == 0 ? 1 : capacity_ * 2;
            resize_storage(new_capacity);
            alloc_traits::construct(allocator_, items_ + size_, std::forward<Args>(args)...);
            ++size_;
        }
        return items_[size_ - 1];
    }

    Iterator Insert(ConstIterator pos, const Type& value) {
        assert(pos >= begin() && pos <= end());
        size_t index = pos - begin();
        
        if (size_ == capacity_) {
            size_t new_capacity = capacity_ == 0 ? 1 : capacity_ * 2;
            resize_storage(new_capacity);
        }
        
        Iterator insert_pos = begin() + index;
        if (insert_pos != end()) {
            // Создаём новый элемент в конце
            alloc_traits::construct(allocator_, items_ + size_, std::move(*(end() - 1)));
            // Сдвигаем элементы вправо
            std::move_backward(insert_pos, end() - 1, end());
            *insert_pos = value;
        } else {
            alloc_traits::construct(allocator_, items_ + size_, value);
        }
        ++size_;
        return insert_pos;
    }

    Iterator Insert(ConstIterator pos, Type&& value) {
        assert(pos >= begin() && pos <= end());
        size_t index = pos - begin();
        
        if (size_ == capacity_) {
            size_t new_capacity = capacity_ == 0 ? 1 : capacity_ * 2;
            resize_storage(new_capacity);
        }
        
        Iterator insert_pos = begin() + index;
        if (insert_pos != end()) {
            // Создаём новый элемент в конце
            alloc_traits::construct(allocator_, items_ + size_, std::move(*(end() - 1)));
            // Сдвигаем элементы вправо
            std::move_backward(insert_pos, end() - 1, end());
            *insert_pos = std::move(value);
        } else {
            alloc_traits::construct(allocator_, items_ + size_, std::move(value));
        }
        ++size_;
        return insert_pos;
    }

    template <typename... Args>
    Iterator Emplace(ConstIterator pos, Args&&... args) {
        assert(pos >= begin() && pos <= end());
        size_t index = pos - begin();
        
        if (size_ == capacity_) {
            size_t new_capacity = capacity_ == 0 ? 1 : capacity_ * 2;
            resize_storage(new_capacity);
        }
        
        Iterator insert_pos = begin() + index;
        if (insert_pos != end()) {
            // Создаём новый элемент в конце
            alloc_traits::construct(allocator_, items_ + size_, std::move(*(end() - 1)));
            // Сдвигаем элементы вправо
            std::move_backward(insert_pos, end() - 1, end());
            // Уничтожаем старый элемент и создаём новый на его месте
            alloc_traits::destroy(allocator_, insert_pos);
            alloc_traits::construct(allocator_, insert_pos, std::forward<Args>(args)...);
        } else {
            alloc_traits::construct(allocator_, items_ + size_, std::forward<Args>(args)...);
        }
        ++size_;
        return insert_pos;
    }

    void PopBack() noexcept {
        if (!IsEmpty()) {
            alloc_traits::destroy(allocator_, items_ + size_ - 1);
            --size_;
        }
    }
    
    Iterator Erase(ConstIterator pos) {
        assert(pos >= begin() && pos < end());
        Iterator erase_pos = const_cast<Iterator>(pos);
        
        alloc_traits::destroy(allocator_, erase_pos);
        if (erase_pos != end() - 1) {
            std::move(erase_pos + 1, end(), erase_pos);
        }
        --size_;
        return erase_pos;
    }
    
    void swap(SimpleVector& other) noexcept {
        using std::swap;
        
        if constexpr (alloc_traits::propagate_on_container_swap::value) {
            swap(allocator_, other.allocator_);
        } else {
            assert(allocator_ == other.allocator_);
        }
        
        swap(items_, other.items_);
        swap(size_, other.size_);
        swap(capacity_, other.capacity_);
    }

    size_t GetSize() const noexcept {
        return size_;
    }

    size_t GetCapacity() const noexcept {
        return capacity_;
    }

    bool IsEmpty() const noexcept {
        return size_ == 0;
    }

    Type& operator[](size_t index) noexcept {
        assert(index < size_);
        return items_[index];
    }

    const Type& operator[](size_t index) const noexcept {
        assert(index < size_);
        return items_[index];
    }

    Type& At(size_t index) {
        if (index >= size_) {
            throw std::out_of_range("Index out of range");
        }
        return items_[index];
    }

    const Type& At(size_t index) const {
        if (index >= size_) {
            throw std::out_of_range("Index out of range");
        }
        return items_[index];
    }

    void Clear() noexcept {
        destroy_elements();
        size_ = 0;
    }

    void Resize(size_t new_size) {
        if (new_size > size_) {
            if (new_size > capacity_) {
                resize_storage(new_size);
            }
            // Конструируем новые элементы
            for (size_t i = size_; i < new_size; ++i) {
                alloc_traits::construct(allocator_, items_ + i);
            }
        } else if (new_size < size_) {
            // Уничтожаем лишние элементы
            for (size_t i = new_size; i < size_; ++i) {
                alloc_traits::destroy(allocator_, items_ + i);
            }
        }
        size_ = new_size;
    }

    void Resize(size_t new_size, const Type& value) {
        if (new_size > size_) {
            if (new_size > capacity_) {
                resize_storage(new_size);
            }
            // Конструируем новые элементы с заданным значением
            for (size_t i = size_; i < new_size; ++i) {
                alloc_traits::construct(allocator_, items_ + i, value);
            }
        } else if (new_size < size_) {
            // Уничтожаем лишние элементы
            for (size_t i = new_size; i < size_; ++i) {
                alloc_traits::destroy(allocator_, items_ + i);
            }
        }
        size_ = new_size;
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity > capacity_) {
            resize_storage(new_capacity);
        }
    }

    Allocator get_allocator() const noexcept {
        return allocator_;
    }

    Iterator begin() noexcept {
        return items_;
    }

    Iterator end() noexcept {
        return items_ + size_;
    }

    ConstIterator begin() const noexcept {
        return items_;
    }

    ConstIterator end() const noexcept {
        return items_ + size_;
    }

    ConstIterator cbegin() const noexcept {
        return items_;
    }

    ConstIterator cend() const noexcept {
        return items_ + size_;
    }

private:
    Type* items_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
    Allocator allocator_;

    void create_storage(size_t capacity) {
        if (capacity > 0) {
            items_ = alloc_traits::allocate(allocator_, capacity);
            capacity_ = capacity;
        }
    }

    void deallocate_storage() {
        if (items_) {
            alloc_traits::deallocate(allocator_, items_, capacity_);
            items_ = nullptr;
            capacity_ = 0;
        }
    }

    void default_construct_elements(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            alloc_traits::construct(allocator_, items_ + i);
        }
    }

    void fill_construct_elements(size_t count, const Type& value) {
        for (size_t i = 0; i < count; ++i) {
            alloc_traits::construct(allocator_, items_ + i, value);
        }
    }

    template <typename InputIt>
    void copy_construct_elements(InputIt first, InputIt last) {
        size_t i = 0;
        for (auto it = first; it != last; ++it, ++i) {
            alloc_traits::construct(allocator_, items_ + i, *it);
        }
    }

    void destroy_elements() {
        for (size_t i = 0; i < size_; ++i) {
            alloc_traits::destroy(allocator_, items_ + i);
        }
    }

    void resize_storage(size_t new_capacity) {
        Type* new_items = alloc_traits::allocate(allocator_, new_capacity);
        
        // Перемещаем существующие элементы
        for (size_t i = 0; i < size_; ++i) {
            alloc_traits::construct(allocator_, new_items + i, std::move_if_noexcept(items_[i]));
            alloc_traits::destroy(allocator_, items_ + i);
        }
        
        if (items_) {
            alloc_traits::deallocate(allocator_, items_, capacity_);
        }
        
        items_ = new_items;
        capacity_ = new_capacity;
    }
};

template <typename Type, typename Allocator>
inline bool operator==(const SimpleVector<Type, Allocator>& lhs, const SimpleVector<Type, Allocator>& rhs) {
    return lhs.GetSize() == rhs.GetSize() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

template <typename Type, typename Allocator>
inline bool operator!=(const SimpleVector<Type, Allocator>& lhs, const SimpleVector<Type, Allocator>& rhs) {
    return !(lhs == rhs);
}

template <typename Type, typename Allocator>
inline bool operator<(const SimpleVector<Type, Allocator>& lhs, const SimpleVector<Type, Allocator>& rhs) {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <typename Type, typename Allocator>
inline bool operator<=(const SimpleVector<Type, Allocator>& lhs, const SimpleVector<Type, Allocator>& rhs) {
    return !(rhs < lhs);
}

template <typename Type, typename Allocator>
inline bool operator>(const SimpleVector<Type, Allocator>& lhs, const SimpleVector<Type, Allocator>& rhs) {
    return rhs < lhs;
}

template <typename Type, typename Allocator>
inline bool operator>=(const SimpleVector<Type, Allocator>& lhs, const SimpleVector<Type, Allocator>& rhs) {
    return !(lhs < rhs);
}

ReserveProxyObj Reserve(size_t capacity_to_reserve) {
    return ReserveProxyObj(capacity_to_reserve);
}