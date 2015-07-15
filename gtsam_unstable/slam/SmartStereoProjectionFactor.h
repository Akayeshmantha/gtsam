/* ----------------------------------------------------------------------------
 
 * GTSAM Copyright 2010, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)
 
 * See LICENSE for the license information
 
 * -------------------------------------------------------------------------- */

/**
 * @file   SmartStereoProjectionFactor.h
 * @brief  Base class to create smart factors on poses or cameras
 * @author Luca Carlone
 * @author Zsolt Kira
 * @author Frank Dellaert
 */

#pragma once

#include <gtsam/slam/SmartFactorBase.h>

#include <gtsam/geometry/triangulation.h>
#include <gtsam/geometry/Pose3.h>
#include <gtsam/geometry/StereoCamera.h>
#include <gtsam/inference/Symbol.h>
#include <gtsam/slam/dataset.h>

#include <boost/optional.hpp>
#include <boost/make_shared.hpp>
#include <vector>

namespace gtsam {

/// Linearization mode: what factor to linearize to
 enum LinearizationMode {
   HESSIAN, IMPLICIT_SCHUR, JACOBIAN_Q, JACOBIAN_SVD
 };

/// How to manage degeneracy
enum DegeneracyMode {
   IGNORE_DEGENERACY, ZERO_ON_DEGENERACY, HANDLE_INFINITY
 };

 /*
  *  Parameters for the smart stereo projection factors
  */
 class GTSAM_EXPORT SmartStereoProjectionParams {

 public:

   LinearizationMode linearizationMode; ///< How to linearize the factor
   DegeneracyMode degeneracyMode; ///< How to linearize the factor

   /// @name Parameters governing the triangulation
   /// @{
   mutable TriangulationParameters triangulation;
   const double retriangulationThreshold; ///< threshold to decide whether to re-triangulate
   /// @}

   /// @name Parameters governing how triangulation result is treated
   /// @{
   const bool throwCheirality; ///< If true, re-throws Cheirality exceptions (default: false)
   const bool verboseCheirality; ///< If true, prints text for Cheirality exceptions (default: false)
   /// @}


   /// Constructor
   SmartStereoProjectionParams(LinearizationMode linMode = HESSIAN,
       DegeneracyMode degMode = IGNORE_DEGENERACY, bool throwCheirality = false,
       bool verboseCheirality = false) :
       linearizationMode(linMode), degeneracyMode(degMode), retriangulationThreshold(
           1e-5), throwCheirality(throwCheirality), verboseCheirality(
           verboseCheirality) {
   }

   virtual ~SmartStereoProjectionParams() {
   }

   void print(const std::string& str) const {
     std::cout << "linearizationMode: " << linearizationMode << "\n";
     std::cout << "   degeneracyMode: " << degeneracyMode << "\n";
     std::cout << triangulation << std::endl;
   }

   LinearizationMode getLinearizationMode() const {
     return linearizationMode;
   }
   DegeneracyMode getDegeneracyMode() const {
     return degeneracyMode;
   }
   TriangulationParameters getTriangulationParameters() const {
     return triangulation;
   }
   bool getVerboseCheirality() const {
     return verboseCheirality;
   }
   bool getThrowCheirality() const {
     return throwCheirality;
   }
   void setLinearizationMode(LinearizationMode linMode) {
     linearizationMode = linMode;
   }
   void setDegeneracyMode(DegeneracyMode degMode) {
     degeneracyMode = degMode;
   }
   void setRankTolerance(double rankTol) {
     triangulation.rankTolerance = rankTol;
   }
   void setEnableEPI(bool enableEPI) {
     triangulation.enableEPI = enableEPI;
   }
   void setLandmarkDistanceThreshold(double landmarkDistanceThreshold) {
     triangulation.landmarkDistanceThreshold = landmarkDistanceThreshold;
   }
   void setDynamicOutlierRejectionThreshold(double dynOutRejectionThreshold) {
     triangulation.dynamicOutlierRejectionThreshold = dynOutRejectionThreshold;
   }
 };



/**
 * SmartStereoProjectionFactor: triangulates point and keeps an estimate of it around.
 * This factor operates with StereoCamrea. This factor requires that values
 * contains the involved camera poses. Calibration is assumed to be fixed.
*/
template<class CALIBRATION>
class SmartStereoProjectionFactor: public SmartFactorBase<StereoCamera> {
private:

  typedef SmartFactorBase<StereoCamera> Base;
  typedef SmartStereoProjectionFactor<CALIBRATION> This;

protected:

  /// @name Parameters
  /// @{
  const SmartStereoProjectionParams params_;
  /// @}

  /// @name Caching triangulation
  /// @{
  mutable TriangulationResult result_; ///< result from triangulateSafe
  mutable std::vector<Pose3> cameraPosesTriangulation_; ///< current triangulation poses
  /// @}

public:

  /// shorthand for a smart pointer to a factor
  typedef boost::shared_ptr<This> shared_ptr;

  /// Vector of cameras
  typedef CameraSet<StereoCamera> Cameras;

  /**
   * Constructor
   * @param params internal parameters of the smart factors
   */
  SmartStereoProjectionFactor(const SmartStereoProjectionParams& params =
      SmartStereoProjectionParams()) :
      params_(params), //
      result_(TriangulationResult::Degenerate()) {
  }

  /** Virtual destructor */
  virtual ~SmartStereoProjectionFactor() {
  }

  /**
   * print
   * @param s optional string naming the factor
   * @param keyFormatter optional formatter useful for printing Symbols
   */
  void print(const std::string& s = "", const KeyFormatter& keyFormatter =
      DefaultKeyFormatter) const {
    std::cout << s << "SmartStereoProjectionFactor\n";
    std::cout << "linearizationMode:\n" << params_.linearizationMode << std::endl;
    std::cout << "triangulationParameters:\n" << params_.triangulation << std::endl;
    std::cout << "result:\n" << result_ << std::endl;
    Base::print("", keyFormatter);
  }

  /// equals
  virtual bool equals(const NonlinearFactor& p, double tol = 1e-9) const {
    const This *e = dynamic_cast<const This*>(&p);
    return e && params_.linearizationMode == e->params_.linearizationMode
        && Base::equals(p, tol);
  }

  /// Check if the new linearization point_ is the same as the one used for previous triangulation
  bool decideIfTriangulate(const Cameras& cameras) const {
    // several calls to linearize will be done from the same linearization point_, hence it is not needed to re-triangulate
    // Note that this is not yet "selecting linearization", that will come later, and we only check if the
    // current linearization is the "same" (up to tolerance) w.r.t. the last time we triangulated the point_

    size_t m = cameras.size();

    bool retriangulate = false;

    // if we do not have a previous linearization point_ or the new linearization point_ includes more poses
    if (cameraPosesTriangulation_.empty()
        || cameras.size() != cameraPosesTriangulation_.size())
      retriangulate = true;

    if (!retriangulate) {
      for (size_t i = 0; i < cameras.size(); i++) {
        if (!cameras[i].pose().equals(cameraPosesTriangulation_[i],
            params_.retriangulationThreshold)) {
          retriangulate = true; // at least two poses are different, hence we retriangulate
          break;
        }
      }
    }

    if (retriangulate) { // we store the current poses used for triangulation
      cameraPosesTriangulation_.clear();
      cameraPosesTriangulation_.reserve(m);
      for (size_t i = 0; i < m; i++)
        // cameraPosesTriangulation_[i] = cameras[i].pose();
        cameraPosesTriangulation_.push_back(cameras[i].pose());
    }

    return retriangulate; // if we arrive to this point_ all poses are the same and we don't need re-triangulation
  }

//  /// triangulateSafe
//  size_t triangulateSafe(const Values& values) const {
//    return triangulateSafe(this->cameras(values));
//  }

  /// triangulateSafe
  TriangulationResult triangulateSafe(const Cameras& cameras) const {

    size_t m = cameras.size();
    if (m < 2) { // if we have a single pose the corresponding factor is uninformative
      return TriangulationResult::Degenerate();
    }

    bool retriangulate = decideIfTriangulate(cameras);
    if (retriangulate) {
      // We triangulate the 3D position of the landmark
//      try {
//        // std::cout << "triangulatePoint3 i \n" << rankTolerance << std::endl;
//
//        //TODO: Chris will replace this with something else for stereo
////        point_ = triangulatePoint3<CALIBRATION>(cameras, this->measured_,
////            rankTolerance_, enableEPI_);
//
//        // // // Temporary hack to use monocular triangulation
//        std::vector<Point2> mono_measurements;
//        BOOST_FOREACH(const StereoPoint2& sp, this->measured_) {
//          mono_measurements.push_back(sp.point2());
//        }
//
//        std::vector<PinholeCamera<Cal3_S2> > mono_cameras;
//        BOOST_FOREACH(const StereoCamera& camera, cameras) {
//          const Pose3& pose = camera.pose();
//          const Cal3_S2& K = camera.calibration()->calibration();
//          mono_cameras.push_back(PinholeCamera<Cal3_S2>(pose, K));
//        }
//        point_ = triangulatePoint3<PinholeCamera<Cal3_S2> >(mono_cameras, mono_measurements,
//            parameters_.rankTolerance, parameters_.enableEPI);
//
//        // // // End temporary hack
//
//        // FIXME: temporary: triangulation using only first camera
////        const StereoPoint2& z0 = this->measured_.at(0);
////        point_ = cameras[0].backproject(z0);
//
//        degenerate_ = false;
//        cheiralityException_ = false;
//
//        // Check landmark distance and reprojection errors to avoid outliers
//        double totalReprojError = 0.0;
//        size_t i = 0;
//        BOOST_FOREACH(const StereoCamera& camera, cameras) {
//          Point3 cameraTranslation = camera.pose().translation();
//          // we discard smart factors corresponding to points that are far away
//          if (cameraTranslation.distance(point_) > parameters_.landmarkDistanceThreshold) {
//            degenerate_ = true;
//            break;
//          }
//          const StereoPoint2& zi = this->measured_.at(i);
//          try {
//            StereoPoint2 reprojectionError(camera.project(point_) - zi);
//            totalReprojError += reprojectionError.vector().norm();
//          } catch (CheiralityException) {
//            cheiralityException_ = true;
//          }
//          i += 1;
//        }
//        //std::cout << "totalReprojError error: " << totalReprojError << std::endl;
//        // we discard smart factors that have large reprojection error
//        if (parameters_.dynamicOutlierRejectionThreshold > 0
//            && totalReprojError / m > parameters_.dynamicOutlierRejectionThreshold)
//          degenerate_ = true;
//
//      } catch (TriangulationUnderconstrainedException&) {
//        // if TriangulationUnderconstrainedException can be
//        // 1) There is a single pose for triangulation - this should not happen because we checked the number of poses before
//        // 2) The rank of the matrix used for triangulation is < 3: rotation-only, parallel cameras (or motion towards the landmark)
//        // in the second case we want to use a rotation-only smart factor
//        degenerate_ = true;
//        cheiralityException_ = false;
//      } catch (TriangulationCheiralityException&) {
//        // point is behind one of the cameras: can be the case of close-to-parallel cameras or may depend on outliers
//        // we manage this case by either discarding the smart factor, or imposing a rotation-only constraint
//        cheiralityException_ = true;
//      }
    }
    return TriangulationResult(Point3());
  }

  /// triangulate
  bool triangulateForLinearize(const Cameras& cameras) const {
    triangulateSafe(cameras); // imperative, might reset result_
    return (result_);
  }

  /// linearize returns a Hessianfactor that is an approximation of error(p)
  boost::shared_ptr<RegularHessianFactor<Base::Dim> > createHessianFactor(
      const Cameras& cameras, const double lambda = 0.0,  bool diagonalDamping =
          false) const {

    size_t numKeys = this->keys_.size();
    // Create structures for Hessian Factors
    std::vector<Key> js;
    std::vector<Matrix> Gs(numKeys * (numKeys + 1) / 2);
    std::vector<Vector> gs(numKeys);

    if (this->measured_.size() != cameras.size()) {
      std::cout
          << "SmartStereoProjectionHessianFactor: this->measured_.size() inconsistent with input"
          << std::endl;
      exit(1);
    }

    triangulateSafe(cameras);

    if (params_.degeneracyMode == ZERO_ON_DEGENERACY && !result_) {
      // failed: return"empty" Hessian
      BOOST_FOREACH(Matrix& m, Gs)
        m = zeros(Base::Dim, Base::Dim);
      BOOST_FOREACH(Vector& v, gs)
        v = zero(Base::Dim);
      return boost::make_shared<RegularHessianFactor<Base::Dim> >(this->keys_,
          Gs, gs, 0.0);
    }

    // Jacobian could be 3D Point3 OR 2D Unit3, difference is E.cols().
    std::vector<typename Base::MatrixZD> Fblocks;
    Matrix F, E;
    Vector b;
    computeJacobiansWithTriangulatedPoint(Fblocks, E, b, cameras);

    // Whiten using noise model
    Base::whitenJacobians(Fblocks, E, b);

    // build augmented hessian
    SymmetricBlockMatrix augmentedHessian = //
        Cameras::SchurComplement(Fblocks, E, b, lambda, diagonalDamping);

    return boost::make_shared<RegularHessianFactor<Base::Dim> >(this->keys_,
        augmentedHessian);
  }

  // create factor
//  boost::shared_ptr<RegularImplicitSchurFactor<StereoCamera> > createRegularImplicitSchurFactor(
//      const Cameras& cameras, double lambda) const {
//    if (triangulateForLinearize(cameras))
//      return Base::createRegularImplicitSchurFactor(cameras, *result_, lambda);
//    else
//      // failed: return empty
//      return boost::shared_ptr<RegularImplicitSchurFactor<StereoCamera> >();
//  }
//
//  /// create factor
//  boost::shared_ptr<JacobianFactorQ<Base::Dim, Base::ZDim> > createJacobianQFactor(
//      const Cameras& cameras, double lambda) const {
//    if (triangulateForLinearize(cameras))
//      return Base::createJacobianQFactor(cameras, *result_, lambda);
//    else
//      // failed: return empty
//      return boost::make_shared<JacobianFactorQ<Base::Dim, Base::ZDim> >(this->keys_);
//  }
//
//  /// Create a factor, takes values
//  boost::shared_ptr<JacobianFactorQ<Base::Dim, Base::ZDim> > createJacobianQFactor(
//      const Values& values, double lambda) const {
//    return createJacobianQFactor(this->cameras(values), lambda);
//  }

  /// different (faster) way to compute Jacobian factor
  boost::shared_ptr<JacobianFactor> createJacobianSVDFactor(
      const Cameras& cameras, double lambda) const {
    if (triangulateForLinearize(cameras))
      return Base::createJacobianSVDFactor(cameras, *result_, lambda);
    else
      return boost::make_shared<JacobianFactorSVD<Base::Dim, ZDim> >(this->keys_);
  }

  /// linearize to a Hessianfactor
  virtual boost::shared_ptr<RegularHessianFactor<Base::Dim> > linearizeToHessian(
      const Values& values, double lambda = 0.0) const {
    return createHessianFactor(this->cameras(values), lambda);
  }

//  /// linearize to an Implicit Schur factor
//  virtual boost::shared_ptr<RegularImplicitSchurFactor<StereoCamera> > linearizeToImplicit(
//      const Values& values, double lambda = 0.0) const {
//    return createRegularImplicitSchurFactor(this->cameras(values), lambda);
//  }
//
//  /// linearize to a JacobianfactorQ
//  virtual boost::shared_ptr<JacobianFactorQ<Base::Dim, Base::ZDim> > linearizeToJacobian(
//      const Values& values, double lambda = 0.0) const {
//    return createJacobianQFactor(this->cameras(values), lambda);
//  }

  /**
   * Linearize to Gaussian Factor
   * @param values Values structure which must contain camera poses for this factor
   * @return a Gaussian factor
   */
  boost::shared_ptr<GaussianFactor> linearizeDamped(const Cameras& cameras,
      const double lambda = 0.0) const {
    // depending on flag set on construction we may linearize to different linear factors
    switch (params_.linearizationMode) {
    case HESSIAN:
      return createHessianFactor(cameras, lambda);
//    case IMPLICIT_SCHUR:
//      return createRegularImplicitSchurFactor(cameras, lambda);
//    case JACOBIAN_SVD:
//      return createJacobianSVDFactor(cameras, lambda);
//    case JACOBIAN_Q:
//      return createJacobianQFactor(cameras, lambda);
    default:
      throw std::runtime_error("SmartStereoFactorlinearize: unknown mode");
    }
  }

  /**
   * Linearize to Gaussian Factor
   * @param values Values structure which must contain camera poses for this factor
   * @return a Gaussian factor
   */
  boost::shared_ptr<GaussianFactor> linearizeDamped(const Values& values,
      const double lambda = 0.0) const {
    // depending on flag set on construction we may linearize to different linear factors
    Cameras cameras = this->cameras(values);
    return linearizeDamped(cameras, lambda);
  }

  /// linearize
  virtual boost::shared_ptr<GaussianFactor> linearize(
      const Values& values) const {
    return linearizeDamped(values);
  }

  /**
   * Triangulate and compute derivative of error with respect to point
   * @return whether triangulation worked
   */
  bool triangulateAndComputeE(Matrix& E, const Cameras& cameras) const {
    bool nonDegenerate = triangulateForLinearize(cameras);
    if (nonDegenerate)
      cameras.project2(*result_, boost::none, E);
    return nonDegenerate;
  }

  /**
   * Triangulate and compute derivative of error with respect to point
   * @return whether triangulation worked
   */
  bool triangulateAndComputeE(Matrix& E, const Values& values) const {
    Cameras cameras = this->cameras(values);
    return triangulateAndComputeE(E, cameras);
  }


  /// Compute F, E only (called below in both vanilla and SVD versions)
  /// Assumes the point has been computed
  /// Note E can be 2m*3 or 2m*2, in case point is degenerate
  void computeJacobiansWithTriangulatedPoint(
      std::vector<typename Base::MatrixZD>& Fblocks, Matrix& E, Vector& b,
      const Cameras& cameras) const {

    if (!result_) {
      throw ("computeJacobiansWithTriangulatedPoint");
//      // Handle degeneracy
//      // TODO check flag whether we should do this
//      Unit3 backProjected; /* = cameras[0].backprojectPointAtInfinity(
//          this->measured_.at(0)); */
//
//      Base::computeJacobians(Fblocks, E, b, cameras, backProjected);
    } else {
      // valid result: just return Base version
      Base::computeJacobians(Fblocks, E, b, cameras, *result_);
    }
  }

  /// Version that takes values, and creates the point
  bool triangulateAndComputeJacobians(
      std::vector<typename Base::MatrixZD>& Fblocks, Matrix& E, Vector& b,
      const Values& values) const {
    Cameras cameras = this->cameras(values);
    bool nonDegenerate = triangulateForLinearize(cameras);
    if (nonDegenerate)
      computeJacobiansWithTriangulatedPoint(Fblocks, E, b, cameras);
    return nonDegenerate;
  }

  /// takes values
  bool triangulateAndComputeJacobiansSVD(
      std::vector<typename Base::MatrixZD>& Fblocks, Matrix& Enull, Vector& b,
      const Values& values) const {
    Cameras cameras = this->cameras(values);
    bool nonDegenerate = triangulateForLinearize(cameras);
    if (nonDegenerate)
      Base::computeJacobiansSVD(Fblocks, Enull, b, cameras, *result_);
    return nonDegenerate;
  }

  /// Calculate vector of re-projection errors, before applying noise model
  Vector reprojectionErrorAfterTriangulation(const Values& values) const {
    Cameras cameras = this->cameras(values);
    bool nonDegenerate = triangulateForLinearize(cameras);
    if (nonDegenerate)
      return Base::unwhitenedError(cameras, *result_);
    else
      return zero(cameras.size() * Base::ZDim);
  }

  /**
   * Calculate the error of the factor.
   * This is the log-likelihood, e.g. \f$ 0.5(h(x)-z)^2/\sigma^2 \f$ in case of Gaussian.
   * In this class, we take the raw prediction error \f$ h(x)-z \f$, ask the noise model
   * to transform it to \f$ (h(x)-z)^2/\sigma^2 \f$, and then multiply by 0.5.
   */
  double totalReprojectionError(const Cameras& cameras,
      boost::optional<Point3> externalPoint = boost::none) const {

    if (externalPoint)
      result_ = TriangulationResult(*externalPoint);
    else
      result_ = triangulateSafe(cameras);

    if (result_)
      // All good, just use version in base class
      return Base::totalReprojectionError(cameras, *result_);
    else if (params_.degeneracyMode == HANDLE_INFINITY) {
      throw("Backproject at infinity");
//      // Otherwise, manage the exceptions with rotation-only factors
//      const StereoPoint2& z0 = this->measured_.at(0);
//      Unit3 backprojected; //= cameras.front().backprojectPointAtInfinity(z0);
//
//      return Base::totalReprojectionError(cameras, backprojected);
    } else
      // if we don't want to manage the exceptions we discard the factor
      return 0.0;
  }

  /// Calculate total reprojection error
  virtual double error(const Values& values) const {
    if (this->active(values)) {
      return totalReprojectionError(Base::cameras(values));
    } else { // else of active flag
      return 0.0;
    }
  }

  /** return the landmark */
    TriangulationResult point() const {
      return result_;
    }

    /** COMPUTE the landmark */
    TriangulationResult point(const Values& values) const {
      Cameras cameras = this->cameras(values);
      return triangulateSafe(cameras);
    }

    /// Is result valid?
    bool isValid() const {
      return result_;
    }

    /** return the degenerate state */
    bool isDegenerate() const {
      return result_.degenerate();
    }

    /** return the cheirality status flag */
    bool isPointBehindCamera() const {
      return result_.behindCamera();
    }

private:

  /// Serialization function
  friend class boost::serialization::access;
  template<class ARCHIVE>
  void serialize(ARCHIVE & ar, const unsigned int /*version*/) {
    ar & BOOST_SERIALIZATION_BASE_OBJECT_NVP(Base);
    ar & BOOST_SERIALIZATION_NVP(params_.throwCheirality);
    ar & BOOST_SERIALIZATION_NVP(params_.verboseCheirality);
  }
};

/// traits
template<class CALIBRATION>
struct traits<SmartStereoProjectionFactor<CALIBRATION> > : public Testable<
    SmartStereoProjectionFactor<CALIBRATION> > {
};

} // \ namespace gtsam
