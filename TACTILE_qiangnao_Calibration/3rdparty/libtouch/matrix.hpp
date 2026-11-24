#ifndef TOUCH_MATRIX_HPP
#define TOUCH_MATRIX_HPP

#include <cassert>
#include <cstring>

namespace touch
{
    // create entity
    template<typename T> void matrix<T>::create(int M, int N)
    {
        assert(M * N != 0);

        rows = M;
        cols = N;

        if (M * N > 0) ptr_ = std::make_shared<T[]>(M * N);
    }

    template<typename T> void matrix<T>::fill(T S) { std::fill(ptr(), ptr() + rows * cols, S); }

    template<typename T> matrix<T> matrix<T>::clone() const
    {
        matrix<T> m(rows, cols);

        std::memcpy(m.ptr(), ptr(), rows * cols * sizeof(T));

        return m;
    }

    // constructors
    template<typename T> matrix<T>::matrix(const int M, const int N) { create(M, N); }

    template<typename T> matrix<T>::matrix(const int M, const int N, T S)
    {
        create(M, N);

        fill(S);
    }

    template<typename T> matrix<T>::matrix(int M, int N, const void *data)
    {
        create(M, N);
        std::memcpy(ptr(), data, M * N * sizeof(T));
    }

    template<typename T> matrix<T>::matrix(const matrix& m) { *this = m; }

    template<typename T> matrix<T>& matrix<T>::operator=(const matrix& m)
    {
        if (this != &m) {
            rows = m.rows;
            cols = m.cols;
            ptr_ = m.ptr_;
        }

        return *this;
    }

    //
    template<typename T> template<typename R> matrix<T>::matrix(const matrix<R>& m) { *this = m; }

    template<typename T> template<typename R> matrix<T>& matrix<T>::operator=(const matrix<R>& m)
    {
        if (rows != m.rows || cols != m.cols) create(m.rows, m.cols);

        for (size_t i = 0; i < m.size(); ++i) {
            ptr()[i] = saturate_cast<T>(m.ptr()[i]);
        }
        return *this;
    }

    //
    template<typename T> T& matrix<T>::operator()(int m, int n)
    {
        assert(m >= 0 && m <= rows && n >= 0 && n <= cols);

        return ptr_[m * cols + n];
    }

    template<typename T> const T& matrix<T>::operator()(int m, int n) const
    {
        assert(m >= 0 && m <= rows && n >= 0 && n <= cols);

        return ptr_[m * cols + n];
    }

    template<typename T> T *matrix<T>::ptr(int m)
    {
        assert(m >= 0 && m <= rows);

        return ptr() + m * cols;
    }

    template<typename T> const T *matrix<T>::ptr(int m) const
    {
        assert(m >= 0 && m <= rows);

        return ptr() + m * cols;
    }

    template<typename T> T *matrix<T>::ptr(int m, int n)
    {
        assert(m >= 0 && m <= rows && n >= 0 && n <= cols);

        return ptr() + m * cols + n;
    }

    template<typename T> const T *matrix<T>::ptr(int m, int n) const
    {
        assert(m >= 0 && m <= rows && n >= 0 && n <= cols);

        return ptr() + m * cols + n;
    }

    template<typename T> matrix<T> matrix<T>::zeros(int M, int N)
    {
        matrix<T> m(M, N);
        std::memset(m.ptr(), 0, M * N * sizeof(T));
        return m;
    }

    template<typename T> matrix<T> matrix<T>::ones(int M, int N) { return matrix<T>(M, N, T(1)); }
} // namespace touch

namespace touch
{
    //
    template<typename T> matrix<T> max(const matrix<T>& m1, const matrix<T>& m2)
    {
        assert(m1.rows == m2.rows && m1.cols == m2.cols);

        matrix<T> m(m1.rows, m2.cols);

        for (size_t i = 0; i < m1.size(); i++) {
            m.ptr()[i] = std::max(m1.ptr()[i], m2.ptr()[i]);
        }

        return m;
    }

    template<typename T> matrix<T> min(const matrix<T>& m1, const matrix<T>& m2)
    {
        assert(m1.rows == m2.rows && m1.cols == m2.cols);

        matrix<T> m(m1.rows, m2.cols);

        for (size_t i = 0; i < m1.size(); i++) {
            m.ptr()[i] = std::min(m1.ptr()[i], m2.ptr()[i]);
        }

        return m;
    }

    // +
    template<class T> matrix<T> operator+(const matrix<T>& m1, const matrix<T>& m2)
    {
        assert(m1.rows == m2.rows && m1.cols == m2.cols);

        matrix<T> m(m1.rows, m2.cols);

        for (size_t i = 0; i < m1.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(m1.ptr()[i] + m2.ptr()[i]);
        }

        return m;
    }

    template<class T, typename S> matrix<T> operator+(const matrix<T>& m1, S delta)
    {
        assert(m1.ptr() != nullptr);

        matrix<T> m(m1.rows, m1.cols);

        for (size_t i = 0; i < m1.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(m1.ptr()[i] + delta);
        }

        return m;
    }
    template<class T, typename S> matrix<T> operator+(S delta, const matrix<T>& m2)
    {
        assert(m2.ptr() != nullptr);

        matrix<T> m(m2.rows, m2.cols);

        for (size_t i = 0; i < m2.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(m2.ptr()[i] + delta);
        }

        return m;
    }

    template<class T> matrix<T>& operator+=(matrix<T>& m1, const matrix<T>& m2)
    {
        assert(m1.rows == m2.rows && m1.cols == m2.cols);

        for (size_t i = 0; i < m2.size(); i++) {
            m1.ptr()[i] = saturate_cast<T>(m1.ptr()[i] + m2.ptr()[i]);
        }

        return m1;
    }

    template<class T, typename S> matrix<T>& operator+=(matrix<T>& m, S delta)
    {
        assert(m.ptr() != nullptr);

        for (size_t i = 0; i < m.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(m.ptr()[i] + delta);
        }

        return m;
    }

    // -
    template<class T> matrix<T> operator-(const matrix<T>& m1, const matrix<T>& m2)
    {
        assert(m1.rows == m2.rows && m1.cols == m2.cols);

        matrix<T> m(m1.rows, m2.cols);

        for (size_t i = 0; i < m1.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(m1.ptr()[i] - m2.ptr()[i]);
        }

        return m;
    }

    template<class T, typename S> matrix<T> operator-(const matrix<T>& m1, S delta)
    {
        assert(m1.ptr() != nullptr);

        matrix<T> m(m1.rows, m1.cols);

        for (size_t i = 0; i < m1.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(m1.ptr()[i] - delta);
        }

        return m;
    }

    template<class T, typename S> matrix<T> operator-(S delta, const matrix<T>& m2)
    {
        assert(m2.ptr() != nullptr);

        matrix<T> m(m2.rows, m2.cols);

        for (size_t i = 0; i < m2.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(delta - m2.ptr()[i]);
        }

        return m;
    }

    template<class T> matrix<T>& operator-=(matrix<T>& m1, const matrix<T>& m2)
    {
        assert(m1.rows == m2.rows && m1.cols == m2.cols);

        for (size_t i = 0; i < m1.size(); i++) {
            m1.ptr()[i] = saturate_cast<T>(m1.ptr()[i] + m2.ptr()[i]);
        }

        return m1;
    }

    template<class T, typename S> matrix<T>& operator-=(matrix<T>& m, S delta)
    {
        assert(m.ptr() != nullptr);

        for (size_t i = 0; i < m.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(m.ptr()[i] - delta);
        }

        return m;
    }

    // *
    template<class T, typename S> matrix<T> operator*(const matrix<T>& m1, S delta)
    {
        assert(m1.ptr() != nullptr);

        matrix<T> m(m1.rows, m1.cols);

        for (size_t i = 0; i < m1.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(m1.ptr()[i] * delta);
        }

        return m;
    }

    template<class T, typename S> matrix<T> operator*(S delta, const matrix<T>& m) { return m * delta; }

    template<class T, typename S> matrix<T>& operator*=(matrix<T>& m, S delta)
    {
        assert(m.ptr() != nullptr);

        for (size_t i = 0; i < m.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(m.ptr()[i] * delta);
        }

        return m;
    }

    // /
    template<class T, typename S> matrix<T> operator/(const matrix<T>& m1, S delta)
    {
        assert(m1.ptr() != nullptr);

        matrix<T> m(m1.rows, m1.cols);

        for (size_t i = 0; i < m.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(m1.ptr()[i] / delta);
        }

        return m;
    }

    template<class T, typename S> matrix<T> operator/(S delta, const matrix<T>& m2)
    {
        assert(m2.ptr() != nullptr);

        matrix<T> m(m2.rows, m2.cols);

        for (size_t i = 0; i < m.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(delta / m2.ptr()[i]);
        }

        return m;
    }

    template<class T, typename S> matrix<T>& operator/=(matrix<T>& m, S delta)
    {
        assert(m.ptr() != nullptr);

        for (size_t i = 0; i < m.size(); i++) {
            m.ptr()[i] = saturate_cast<T>(m.ptr()[i] / delta);
        }

        return m;
    }
} // namespace touch

#endif //! TOUCH_MATRIX_HPP
