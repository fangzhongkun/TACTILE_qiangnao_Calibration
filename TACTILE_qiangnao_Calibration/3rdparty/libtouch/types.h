#ifndef TOUCH_TYPES_H
#define TOUCH_TYPES_H

#include <chrono>
#include <memory>

namespace touch
{
    //
    template<typename T> struct size
    {
        T width{};
        T height{};
    };

    template<typename T> bool operator==(const size<T>& l, const size<T>& r);
    template<typename T> bool operator!=(const size<T>& l, const size<T>& r);

    //
    template<typename T> struct ratio
    {
        T num{ 0 };
        T den{ 1 };
    };

    template<typename T> bool operator==(const ratio<T>& l, const ratio<T>& r);
    template<typename T> bool operator!=(const ratio<T>& l, const ratio<T>& r);

    //
    template<typename T> struct point
    {
        T x{};
        T y{};
    };

    template<typename T> point<T>  operator+(const point<T>& l, const point<T>& r);
    template<typename T> point<T>& operator+=(const point<T>& l, const point<T>& r);

    template<typename T> point<T>  operator-(const point<T>& l, const point<T>& r);
    template<typename T> point<T>& operator-=(const point<T>& l, const point<T>& r);

    template<typename T> bool operator==(const point<T>& l, const point<T>& r);
    template<typename T> bool operator!=(const point<T>& l, const point<T>& r);

    template<typename T> double   distance(const point<T>& p1, const point<T>& p2);
    template<typename T> point<T> intermediate(const point<T>& p1, const point<T>& p2);

    //
    template<typename T> struct rect
    {
        T left() const { return x; }
        T top() const { return y; }
        T right() const { return x + static_cast<T>(width) - 1; }
        T bottom() const { return y + static_cast<T>(height) - 1; }

        bool contains(T x, T y, bool = false) const;
        bool contains(const point<T>& p, bool = false) const;
        bool contains(const rect<T>& r, bool = false) const;

        T x{};
        T y{};
        T width{};
        T height{};
    };

    template<typename T> bool operator==(const rect<T>& l, const rect<T>& r);
    template<typename T> bool operator!=(const rect<T>& l, const rect<T>& r);

    //
    template<typename T> struct vector
    {
        vector() = default;

        explicit vector(size_t N);
        vector(size_t N, const T& v);

        vector(const vector& v);
        vector<T>& operator=(const vector& v);

        ~vector() = default;

        //
        void create(size_t N);

        //
        T       *ptr() { return ptr_.get(); }
        const T *ptr() const { return ptr_.get(); }

        //
        T&       operator[](size_t i);
        const T& operator[](size_t i) const;

        size_t size{};

    private:
        std::shared_ptr<T[]> ptr_{};
    };

    template<typename T> bool operator==(const vector<T>& l, const vector<T>& r);
    template<typename T> bool operator!=(const vector<T>& l, const vector<T>& r);

    //
    template<typename T> struct touch
    {
        point<int> lt{ std::numeric_limits<int>::max(), std::numeric_limits<int>::max() };
        point<int> rb{ 0, 0 };
        size_t     area{};

        point<int> peak{};
        T          value{}; // pressure

        std::chrono::microseconds timestamp{};

        [[nodiscard]] point<int> center() const { return point{ (lt.x + rb.x) / 2, (lt.y + rb.y) / 2 }; }
    };
} // namespace touch

#include "types.hpp"

#endif //! TOUCH_TYPES_H
