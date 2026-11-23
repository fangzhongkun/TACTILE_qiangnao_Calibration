#ifndef TOUCH_MATRIX_H
#define TOUCH_MATRIX_H

#include "saturate.h"

#include <memory>
#include <numeric>

namespace touch
{
    template<typename T> class matrix
    {
    public:
        using value_type = T;

    public:
        matrix() = default;

        matrix(int M, int N);
        matrix(int M, int N, T S);

        matrix(int M, int N, const void *data);

        matrix(const matrix& m);
        matrix& operator=(const matrix& m);

        ~matrix() = default;

        //
        template<typename R> matrix(const matrix<R>& m);
        template<typename R> matrix<T>& operator=(const matrix<R>& m);

        //
        void create(int M, int N);
        void fill(T S);

        matrix<T> clone() const;

        //
        T&       operator()(int m, int n);
        const T& operator()(int m, int n) const;

        T       *data() { return ptr_.get(); };
        const T *data() const { return ptr_.get(); };

        T       *ptr() { return ptr_.get(); };
        const T *ptr() const { return ptr_.get(); };

        T       *ptr(int m);
        const T *ptr(int m) const;

        T       *ptr(int m, int n);
        const T *ptr(int m, int n) const;

        //
        static matrix<T> zeros(int M, int N);

        static matrix<T> ones(int M, int N);

        //
        [[nodiscard]] bool empty() const { return rows <= 0 || cols <= 0 || ptr_ == nullptr; }

        [[nodiscard]] size_t size() const { return static_cast<size_t>(rows * cols); }

        [[nodiscard]] size_t linesize() const { return cols * sizeof(T); }

        //

        T min() const { return *std::min_element(ptr(), ptr() + size()); }

        T max() const { return *std::max_element(ptr(), ptr() + size()); }

        std::pair<T, T> minmax() const
        {
            const auto [i, a] = std::minmax_element(ptr(), ptr() + size());
            return { *i, *a };
        }

        template<typename R> R sum() const { return std::accumulate(ptr(), ptr() + size(), R{ 0 }); }

        template<typename R> R mean() const { return static_cast<R>(sum<R>() / size()); }

        //
        int rows{};
        int cols{};

    private:
        std::shared_ptr<T[]> ptr_{};
    };

    //
    template<typename T> matrix<T> max(const matrix<T>& m1, const matrix<T>& m2);
    template<typename T> matrix<T> min(const matrix<T>& m1, const matrix<T>& m2);

    // +
    template<class T> matrix<T>             operator+(const matrix<T>& m1, const matrix<T>& m2);
    template<class T, typename S> matrix<T> operator+(const matrix<T>& m, S delta);
    template<class T, typename S> matrix<T> operator+(S delta, const matrix<T>& m);

    template<class T> matrix<T>&             operator+=(matrix<T>& m1, const matrix<T>& m2);
    template<class T, typename S> matrix<T>& operator+=(matrix<T>& m, S delta);

    // -
    template<class T> matrix<T>             operator-(const matrix<T>& m1, const matrix<T>& m2);
    template<class T, typename S> matrix<T> operator-(const matrix<T>& m, S delta);
    template<class T, typename S> matrix<T> operator-(S delta, const matrix<T>& m);

    template<class T> matrix<T>&             operator-=(matrix<T>& m1, const matrix<T>& m2);
    template<class T, typename S> matrix<T>& operator-=(matrix<T>& m, S delta);

    // *
    template<class T, typename S> matrix<T> operator*(const matrix<T>& m, S delta);
    template<class T, typename S> matrix<T> operator*(S delta, const matrix<T>& m);

    template<class T, typename S> matrix<T>& operator*=(matrix<T>& m, S delta);

    // /
    template<class T, typename S> matrix<T> operator/(const matrix<T>& m, S delta);
    template<class T, typename S> matrix<T> operator/(S delta, const matrix<T>& m);

    template<class T, typename S> matrix<T>& operator/=(matrix<T>& m, S delta);
} // namespace touch

#include "matrix.hpp"

#endif //! TOUCH_MATRIX_H
