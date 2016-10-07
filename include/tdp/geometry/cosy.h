#pragma once
#include <Eigen/Dense>
#include <tdp/geometry/vectors.h>

namespace tdp {

template<typename T, int Option>
Eigen::Matrix<T,3,3,Option> Orthonormalize(const Eigen::Matrix<T,3,1,Option>& a,
  const Eigen::Matrix<T,3,1,Option>& b) {
  Eigen::Matrix<T,3,3,Option> R;
  R.col(0) = a/a.norm();
  R.col(1) = RejectAfromB(b,a);
  R.col(1) /= R.col(1).norm();
  R.col(2) = R.col(0).cross(R.col(1));
  R.col(2) /= R.col(2).norm();
  return R;
}

template<typename DerivedA, typename DerivedB>
Eigen::Matrix3f OrthonormalizeFromYZ(
    const Eigen::MatrixBase<DerivedA>& y,
    const Eigen::MatrixBase<DerivedB>& z) {
  Eigen::Matrix3f R;
  R.col(2) = Normalize<DerivedB,Eigen::Vector3f>(z.template cast<float>());
  Eigen::Vector3f yOrtho;
  RejectAfromB(y.template cast<float>(), z.template cast<float>(), 
      yOrtho);
  R.col(1) = Normalize<DerivedA,Eigen::Vector3f>(yOrtho);
  R.col(0) = Normalize<Eigen::Vector3f,Eigen::Vector3f>(R.col(1).cross(R.col(2)));
  return R;
}

}
