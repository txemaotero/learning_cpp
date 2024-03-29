#pragma once

#include <cstddef>
#include <functional>
#include <numeric>

#include "matrix_fwd.hpp"
#include "matrix_slice.hpp"
#include "slice.hpp"
#include "traits.hpp"

namespace matrix_impl {
// Compute the strides of the matrix given the extents and returns the total
// number of elements
template <size_t N>
size_t compute_strides(const std::array<size_t, N> &extents,
                       std::array<size_t, N> &strides) {
  size_t size = 1;
  for (int i = N - 1; i >= 0; --i) {
    strides[i] = size;
    size *= extents[i];
  }
  return size;
}

template <size_t N> size_t compute_size(const std::array<size_t, N> &extents) {
  return std::accumulate(extents.begin(), extents.end(), 1,
                         std::multiplies<size_t>());
}

template <typename T, std::size_t N> struct MatrixInit {
  using type = std::initializer_list<typename MatrixInit<T, N - 1>::type>;
};

// The N == 1 is special. That's where we got to the (most deeply nested).
// std::initialize_list<T>
template <typename T> struct MatrixInit<T, 1> {
  using type = std::initializer_list<T>;
};

// To avoid surprices, we define N = 0 to be an error
template <typename T> struct MatrixInit<T, 0>; // undefined on purpose

template <std::size_t N, typename I, typename T>
Enable_if<(N == 1), void> add_extents(I &first,
                                      const std::initializer_list<T> &list) {
  *first = list.size();
}

template <std::size_t N, typename List> bool check_non_jagged(const List &list);

template <std::size_t N, typename I, typename List>
Enable_if<(N > 1), void> add_extents(I &first, const List &list) {
  assert(check_non_jagged<N>(list));
  *first++ = list.size();
  add_extents<N - 1>(first, *list.begin());
}

template <std::size_t N, typename List>
std::array<std::size_t, N> derive_extents(const List &list) {
  std::array<std::size_t, N> a;
  auto f = a.begin();
  add_extents<N>(f, list);
  return a;
}

// Checks that all rows have the same number of elements.
template <std::size_t N, typename List>
bool check_non_jagged(const List &list) {
  auto i = list.begin();
  for (auto j = i + 1; j != list.end(); ++j) {
    if (derive_extents<N - 1>(*i) != derive_extents<N - 1>(*j))
      return false;
  }
  return true;
}

template <typename T, typename Vec>
void add_list(std::initializer_list<T> *first, std::initializer_list<T> *last,
              Vec &vec) {
  for (; first != last; ++first)
    add_list(first->begin(), first->end(), vec);
}

template <typename T, typename Vec>
void add_list(const T *first, const T *last, Vec &vec) {
  vec.insert(vec.end(), first, last);
}

// Put the elements of the initializer_list into the vector
template <typename T, typename Vec>
void insert_flat(std::initializer_list<T> list, Vec &vec) {
  add_list(list.begin(), list.end(), vec);
}

// Get a row (or column or whatever the I dimension index stablish) with
// the given index "offset" from the orig matrix slice and put it in the dest
// matrix slice
template <std::size_t I, std::size_t N>
void slice_dim(std::size_t offset, const MatrixSlice<N> &desc,
               MatrixSlice<N - 1> &row) {
  row.start = desc.start;

  int j = (int)N - 2;
  for (int i = N - 1; i >= 0; --i) {
    if (i == I)
      row.start += desc.strides[i] * offset;
    else {
      row.extents[j] = desc.extents[i];
      row.strides[j] = desc.strides[i];
      --j;
    }
  }

  row.size = compute_size(row.extents);
}

template <typename... Args> constexpr bool Requesting_element() {
  return All(Convertible<Args, size_t>()...);
}

template <typename... Args> constexpr bool Requesting_slice() {
  return All((Convertible<Args, size_t>() || Same<Args, slice>())...) &&
         Some(Same<Args, slice>()...);
}

template <std::size_t N, typename... Dims>
bool check_bounds(const MatrixSlice<N> &ms, Dims... dims) {
  std::size_t indexes[N]{std::size_t(dims)...};
  return std::equal(indexes, indexes + N, ms.extents.begin(),
                    std::less<std::size_t>{});
}

template <std::size_t NRest, std::size_t N>
std::size_t do_slice_dim(const MatrixSlice<N> &os, MatrixSlice<N> &ns,
                         std::size_t s) {
  std::size_t i = N - NRest;
  ns.strides[i] = os.strides[i];
  ns.extents[i] = 1;
  return s * ns.strides[i];
}

template <std::size_t NRest, std::size_t N>
std::size_t do_slice_dim(const MatrixSlice<N> &os, MatrixSlice<N> &ns,
                         slice s) {
  std::size_t i = N - NRest;
  ns.strides[i] = s.stride * os.strides[i];
  ns.extents[i] = (s.length == size_t(-1))
                      ? (os.extents[i] - s.start + s.stride - 1) / s.stride
                      : s.length;
  return s.start * os.strides[i];
}

template <std::size_t N>
std::size_t do_slice_dim2(const MatrixSlice<N> &os, MatrixSlice<N> &ns, slice s,
                          std::size_t NRest) {
  std::size_t i = N - NRest;
  ns.strides[i] = s.stride * os.strides[i];
  ns.extents[i] = (s.length == size_t(-1))
                      ? (os.extents[i] - s.start + s.stride - 1) / s.stride
                      : s.length;
  return s.start * os.strides[i];
}

template <std::size_t N>
std::size_t do_slice(const MatrixSlice<N> &os, MatrixSlice<N> &ns) {
  ignore(os);
  ignore(ns);
  return 0;
}

template <std::size_t N, typename T, typename... Args>
std::size_t do_slice(const MatrixSlice<N> &os, MatrixSlice<N> &ns, const T &s,
                     const Args &...args) {
  std::size_t m = do_slice_dim<sizeof...(Args) + 1>(os, ns, s);
  std::size_t n = do_slice(os, ns, args...);
  return m + n;
}

template <typename T, typename Iter>
void copy_list(const T *first, const T *last, Iter &iter) {
  iter = std::copy(first, last, iter);
}

template <typename T, typename Iter>
void copy_list(const std::initializer_list<T> *first,
               const std::initializer_list<T> *last, Iter &it) {
  for (; first != last; ++first)
    copy_list(first->begin(), first->end(), it);
}

template <typename T, typename Iter>
void copy_flat(std::initializer_list<T> list, Iter &iter) {
  copy_list(list.begin(), list.end(), iter);
}

} // namespace matrix_impl

template <typename T, std::size_t N>
using MatrixInitializer = typename matrix_impl::MatrixInit<T, N>::type;
