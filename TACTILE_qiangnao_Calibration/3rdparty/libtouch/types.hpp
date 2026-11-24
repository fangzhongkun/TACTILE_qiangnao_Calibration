#ifndef TOUCH_TYPES_HPP
#define TOUCH_TYPES_HPP

#include <cassert>
#include <cstring>

namespace touch
{
    // size
    template<typename T> bool operator==(const size<T>& l, const size<T>& r)
    {
        return (l.width == r.width) && (l.height == r.height);
    }

    template<typename T> bool operator!=(const size<T>& l, const size<T>& r) { return !(l == r); }

    // ratio
    template<typename T> bool operator==(const ratio<T>& l, const ratio<T>& r)
    {
        return (l.num == r.num) && (l.den == r.den);
    }

    template<typename T> bool operator!=(const ratio<T>& l, const ratio<T>& r) { return !(l == r); }

    // point
    template<typename T> point<T> operator+(const point<T>& l, const point<T>& r)
    {
        return point<T>{ l.x + r.x, l.y + r.y };
    }

    template<typename T> point<T>& operator+=(const point<T>& l, const point<T>& r)
    {
        l.x += r.x;
        l.y += r.y;
        return l;
    }

    template<typename T> point<T> operator-(const point<T>& l, const point<T>& r)
    {
        return point<T>{ l.x - r.x, l.y - r.y };
    }

    template<typename T> point<T>& operator-=(const point<T>& l, const point<T>& r)
    {
        l.x -= r.x;
        l.y -= r.y;
        return l;
    }

    template<typename T> bool operator==(const point<T>& l, const point<T>& r)
    {
        return (l.x == r.x) && (l.y == r.y);
    }

    template<typename T> bool operator!=(const point<T>& l, const point<T>& r) { return !(l == r); }

    template<typename T> double distance(const point<T>& p1, const point<T>& p2)
    {
        double dx = p2.x - p1.x;
        double dy = p2.y - p1.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    template<typename T> point<T> intermediate(const point<T>& p1, const point<T>& p2)
    {
        return point<T>{ (p1.x + p2.x) / 2, (p1.y + p2.y) / 2 };
    }

    // rect

    template<typename T> bool rect<T>::contains(T x, T y, bool proper) const
    {
        return proper ? (x > left() && x < right()) && (y > top() && y < bottom())
                      : (x >= left() && x <= right()) && (y >= top() && y <= bottom());
    }

    template<typename T> bool rect<T>::contains(const point<T>& p, bool proper) const
    {
        return contains(p.x, p.y, proper);
    }

    template<typename T> bool rect<T>::contains(const rect<T>& r, bool proper) const
    {
        return proper
                   ? (left() < r.left() && right() > r.right() && top() < r.top() && bottom() > r.bottom())
                   : (left() <= r.left() && right() >= r.right() && top() <= r.top() &&
                      bottom() >= r.bottom());
    }

    template<typename T> bool operator==(const rect<T>& l, const rect<T>& r)
    {
        return (l.x == r.x) && (l.y == r.y) && (l.width == r.width) && (l.height == r.height);
    }

    template<typename T> bool operator!=(const rect<T>& l, const rect<T>& r) { return !(l == r); }

    //
    template<typename T> void vector<T>::create(size_t N)
    {
        size = N;
        ptr_ = std::make_shared<T[]>(N);
    }

    template<typename T> vector<T>::vector(size_t N) { create(N); }

    template<typename T> vector<T>::vector(size_t N, const T& v)
    {
        create(N);
        std::fill(ptr_, ptr_ + N, v);
    }

    template<typename T> vector<T>::vector(const vector& v) { *this = v; }

    template<typename T> vector<T>& vector<T>::operator=(const vector& v)
    {
        assert(v.size > 0);

        if (this != &v) {
            size = v.size;
            ptr_ = v.ptr_;
        }

        return *this;
    }

    template<typename T> T& vector<T>::operator[](size_t i)
    {
        assert(i >= 0 && i < size);
        return ptr_[i];
    }

    template<typename T> const T& vector<T>::operator[](size_t i) const
    {
        assert(i >= 0 && i < size);
        return ptr_[i];
    }

    template<typename T> bool operator==(const vector<T>& l, const vector<T>& r)
    {
        return std::memcmp(l.ptr(), r.ptr(), l.size * sizeof(T)) == 0;
    }

    template<typename T> bool operator!=(const vector<T>& l, const vector<T>& r) { return !(l == r); }
} // namespace touch

#endif //! TOUCH_TYPES_HPP
