/*
 * complexfun.h
 *
 *  Created on: Jan 28, 2009
 *      Author: eh7
 */

#ifndef COMPLEXFUN_H_
#define COMPLEXFUN_H_

#include <mra/mra.h>

namespace madness {

//***************************************************************************
double abs(double x) {return x;}
//***************************************************************************

//***************************************************************************
double real(double x) {return x;}
//***************************************************************************

//***************************************************************************
double imag(double x) {return 0.0;}
//***************************************************************************

////***************************************************************************
//double conj(double x) {return 0.0;}
////***************************************************************************

//***************************************************************************
template <typename Q>
Tensor< std::complex<Q> > tensor_real2complex(const Tensor<Q>& r)
{
  Tensor< std::complex<Q> > c(r.ndim, r.dim);
  BINARY_OPTIMIZED_ITERATOR(Q, r, std::complex<Q>, c, *_p1 = std::complex<Q>(*_p0,0.0););
  return c;
}
//***************************************************************************

//***************************************************************************
template <typename Q>
Tensor<Q> tensor_real(const Tensor< std::complex<Q> >& c)
{
  Tensor<Q> r(c.ndim, c.dim);
  BINARY_OPTIMIZED_ITERATOR(Q, r, std::complex<Q>, c, *_p0 = real(*_p1););
  return r;
}
//***************************************************************************

//***************************************************************************
template <typename Q>
Tensor<Q> tensor_imag(const Tensor< std::complex<Q> >& c)
{
  Tensor<Q> r(c.ndim, c.dim);
  BINARY_OPTIMIZED_ITERATOR(Q, r, std::complex<Q>, c, *_p0 = imag(*_p1););
  return r;
}
//***************************************************************************

//***************************************************************************
template <typename Q>
Tensor<Q> tensor_abs(const Tensor< std::complex<Q> >& c)
{
  Tensor<Q> r(c.ndim, c.dim);
  BINARY_OPTIMIZED_ITERATOR(Q, r, std::complex<Q>, c, *_p0 = abs(*_p1););
  return r;
}
//***************************************************************************

//***************************************************************************
template <typename Q, int NDIM>
struct abs_square_op
{
  typedef typename TensorTypeData<Q>::scalar_type resultT;
  Tensor<resultT> operator()(const Key<NDIM>& key, const Tensor<Q>& t) const
  {
    Tensor<resultT> result(t.ndim, t.dim);
    BINARY_OPTIMIZED_ITERATOR(Q, t, resultT, result, resultT d = abs(*_p0); *_p1 = d*d);
    return result;
  }
  template <typename Archive>
  void serialize(Archive& ar) {}
};
//***************************************************************************

//***************************************************************************
template<typename Q, int NDIM>
Function<typename TensorTypeData<Q>::scalar_type,NDIM> abs_square(const Function<Q,NDIM>& func)
{
  return unary_op(func, abs_square_op<Q,NDIM>());
}
//***************************************************************************

//***************************************************************************
template <typename Q, int NDIM>
struct real_op
{
  typedef typename TensorTypeData<Q>::scalar_type resultT;
  Tensor<resultT> operator()(const Key<NDIM>& key, const Tensor<Q>& t) const
  {
    Tensor<resultT> result(t.ndim, t.dim);
    BINARY_OPTIMIZED_ITERATOR(Q, t, resultT, result, resultT *_p1 = real(*_p0););
    return result;
  }
  template <typename Archive>
  void serialize(Archive& ar) {}
};
//***************************************************************************

//***************************************************************************
template<typename Q, int NDIM>
Function<typename TensorTypeData<Q>::scalar_type,NDIM> real(const Function<Q,NDIM>& func)
{
  return unary_op_coeffs(func, real_op<Q,NDIM>());
}
//***************************************************************************

//***************************************************************************
template <typename Q, int NDIM>
struct imag_op
{
  typedef typename TensorTypeData<Q>::scalar_type resultT;
  Tensor<resultT> operator()(const Key<NDIM>& key, const Tensor<Q>& t) const
  {
    Tensor<resultT> result(t.ndim, t.dim);
    BINARY_OPTIMIZED_ITERATOR(Q, t, resultT, result, resultT *_p1 = imag(*_p0););
    return result;
  }
  template <typename Archive>
  void serialize(Archive& ar) {}
};
//***************************************************************************

//***************************************************************************
template<typename Q, int NDIM>
Function<typename TensorTypeData<Q>::scalar_type,NDIM> imag(const Function<Q,NDIM>& func)
{
  return unary_op_coeffs(func, imag_op<Q,NDIM>());
}
//***************************************************************************

//***************************************************************************
template <typename Q, int NDIM>
struct conj_op
{
  typedef Q resultT;
  Tensor<resultT> operator()(const Key<NDIM>& key, const Tensor<Q>& t) const
  {
    Tensor<resultT> result(t.ndim, t.dim);
    BINARY_OPTIMIZED_ITERATOR(Q, t, resultT, result, resultT *_p1 = conj(*_p0););
    return result;
  }
  template <typename Archive>
  void serialize(Archive& ar) {}
};
//***************************************************************************

//***************************************************************************
template<typename Q, int NDIM>
Function<Q,NDIM> conj(const Function<Q,NDIM>& func)
{
  return unary_op_coeffs(func, conj_op<Q,NDIM>());
}
//***************************************************************************

//***************************************************************************
template <typename Q, int NDIM>
struct function_real2complex_op
{
  typedef std::complex<Q> resultT;
  Tensor<resultT> operator()(const Key<NDIM>& key, const Tensor<Q>& t) const
  {
    Tensor<resultT> result(t.ndim, t.dim);
    BINARY_OPTIMIZED_ITERATOR(Q, t, resultT, result, *_p1 = resultT(*_p0,0.0););
    return result;
  }
  template <typename Archive>
  void serialize(Archive& ar) {}
};
//***************************************************************************

//***************************************************************************
template <typename Q, int NDIM>
Function<std::complex<Q>,NDIM> function_real2complex(const Function<Q,NDIM>& r)
{
  return unary_op_coeffs(r, function_real2complex_op<Q,NDIM>());
}
//***************************************************************************

}
#endif /* COMPLEXFUN_H_ */
