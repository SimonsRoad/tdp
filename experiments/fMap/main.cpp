/* Copyright (c) 2016, Julian Straub <jstraub@csail.mit.edu> Licensed
 * under the MIT license. See the license file LICENSE.
 */
#include <iostream>
#include <cmath>
#include <complex>
#include <vector>
#include <cstdlib>


#include <pangolin/pangolin.h>
#include <pangolin/video/video_record_repeat.h>
#include <pangolin/gl/gltexturecache.h>
#include <pangolin/gl/glpixformat.h>
#include <pangolin/handler/handler_image.h>
#include <pangolin/utils/file_utils.h>
#include <pangolin/utils/timer.h>
#include <pangolin/gl/gl.h>
#include <pangolin/gl/glsl.h>
#include <pangolin/gl/glvbo.h>
#include <pangolin/gl/gldraw.h>
#include <pangolin/image/image_io.h>

#include <tdp/eigen/dense.h>
#include <Eigen/Eigenvalues>
#include <Eigen/Sparse>

#include <tdp/preproc/depth.h>
#include <tdp/preproc/pc.h>
#include <tdp/camera/camera.h>
#ifdef CUDA_FOUND
#include <tdp/preproc/normals.h>
#endif

#include <tdp/io/tinyply.h>
#include <tdp/gl/shaders.h>
#include <tdp/gl/gl_draw.h>

#include <tdp/gui/gui.hpp>
#include <tdp/gui/quickView.h>

#include <tdp/nn/ann.h>
#include <tdp/manifold/S.h>
#include <tdp/manifold/SE3.h>
#include <tdp/data/managed_image.h>

#include <tdp/utils/status.h>
#include <tdp/utils/timer.hpp>
#include <tdp/eigen/std_vector.h>


#include <tdp/laplace_beltrami/laplace_beltrami.h>

//Given C mtx, x (index to pc_s, Source Manifold) , find y (index to pc_t) the correspondence in the Target Manifold
int getCorrespondence(const tdp::Image<tdp::Vector3fda>& pc_s,
                      const tdp::Image<tdp::Vector3fda>& pc_t,
                      const Eigen::MatrixXf& S_wl,
                      const Eigen::MatrixXf& T_wl,
                      const Eigen::MatrixXf& C,
                      const float alpha,
                      const int query){
    Eigen::VectorXf f_w(pc_s.Area()), g_w(pc_t.Area());
    Eigen::VectorXf f_l(C.rows()), g_l(C.rows());
    int target_r, target_c;
    tdp::f_rbf(pc_s, pc_s[query], alpha, f_w);
    f_l = (f_w.transpose()*S_wl).transpose();
    g_l = C*f_l;
    g_w = T_wl*g_l;
    g_w.maxCoeff(&target_r,&target_c);
    return target_r;
}

int main( int argc, char* argv[] ){

  tdp::ManagedHostImage<tdp::Vector3fda> pc_s(10000,1);
  tdp::ManagedHostImage<tdp::Vector3fda> ns_s(10000,1);

  tdp::ManagedHostImage<tdp::Vector3fda> pc_t(10000,1);
  tdp::ManagedHostImage<tdp::Vector3fda> ns_t(10000,1);


  if (argc > 1) {
      const std::string input = std::string(argv[1]);
      std::cout << "input pc: " << input << std::endl;
      tdp::LoadPointCloud(input, pc_s, ns_s);
  } else {
      std::srand(101);
      GetSphericalPc(pc_s);
      std::srand(200);
      GetSphericalPc(pc_t);
      //GetCylindricalPc(pc);
  }

  // build kd tree
  tdp::ANN ann_s, ann_t;
  ann_s.ComputeKDtree(pc_s);
  ann_t.ComputeKDtree(pc_t);

  // Create OpenGL window - guess sensible dimensions
  int menue_w = 180;
  pangolin::CreateWindowAndBind( "GuiBase", 1200+menue_w, 800);
  // current frame in memory buffer and displaying.
  pangolin::CreatePanel("ui").SetBounds(0.,1.,0.,pangolin::Attach::Pix(menue_w));
  // Assume packed OpenGL data unless otherwise specified
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  // setup container
  pangolin::View& container = pangolin::Display("container");
  container.SetLayout(pangolin::LayoutEqual)
    .SetBounds(0., 1.0, pangolin::Attach::Pix(menue_w), 1.0);
  // Define Camera Render Object (for view / scene browsing)
  pangolin::OpenGlRenderState s_cam(
      pangolin::ProjectionMatrix(640,480,420,420,320,240,0.1,1000),
      pangolin::ModelViewLookAt(0,0.5,-3, 0,0,0, pangolin::AxisNegY)
      );
  pangolin::OpenGlRenderState t_cam(
      pangolin::ProjectionMatrix(640,480,420,420,320,240,0.1,1000),
      pangolin::ModelViewLookAt(0,0.5,-3, 0,0,0, pangolin::AxisNegY)
      );
  pangolin::OpenGlRenderState mtx_cam(
      pangolin::ProjectionMatrix(640,480,420,420,320,240,0.1,1000),
      pangolin::ModelViewLookAt(0,0.5,-3, 0,0,0, pangolin::AxisNegY)
      );
  // Add named OpenGL viewport to window and provide 3D Handler
  pangolin::View& viewPc = pangolin::CreateDisplay()
    .SetHandler(new pangolin::Handler3D(s_cam));
  container.AddDisplay(viewPc);
  pangolin::View& viewPc_t = pangolin::CreateDisplay()
    .SetHandler(new pangolin::Handler3D(t_cam));
  container.AddDisplay(viewPc_t);
  pangolin::View& viewMtx = pangolin::CreateDisplay()
    .SetHandler(new pangolin::Handler3D(mtx_cam));
  container.AddDisplay(viewMtx);

  // Add variables to pangolin GUI
  pangolin::Var<bool> showFMap("ui.show fMap", true, false);
  // pangolin::Var<int> pcOption("ui. pc option", 0, 0,1);
  //-- variables for KNN
  pangolin::Var<int> knn("ui.knn",30,1,100);
  pangolin::Var<float> eps("ui.eps", 1e-6 ,1e-7, 1e-5);
  pangolin::Var<float> alpha("ui. alpha", 0.01, 0.005, 0.3); //variance of rbf kernel
  pangolin::Var<int> numEv("ui.numEv",10,10,300);
  pangolin::Var<int>nBins("ui. nBins", 10, 10,100);
  //-- viz color coding
  pangolin::Var<float>minVal("ui. min Val",-0.71,-1,0);
  pangolin::Var<float>maxVal("ui. max Val",0.01,1,0);
  float minVal_t, maxVal_t, minVal_c, maxVal_c;

  // get the matrix pc for visualizing C
  tdp::ManagedHostImage<tdp::Vector3fda> pc_mtx((int)numEv*(int)numEv,1);
  GetMtxPc(pc_mtx, (int)numEv, (int)numEv);
//  std::cout << "pc mtx: "<< std::endl;
//  for (int r=0; r<(int)numEv; ++r){
//      std::cout << "r: " << r << std::endl;
//      for (int c=0; c<(int)numEv; ++c){
//          std::cout << pc_mtx[r*(int)numEv + c] << std::endl;
//      }
//      std::cout << std::endl;
//  }
  // use those OpenGL buffers
  pangolin::GlBuffer vbo,vbo_t, vbo_c, valuebo_s,valuebo_t, valuebo_c;
  vbo.Reinitialise(pangolin::GlArrayBuffer, pc_s.Area(),  GL_FLOAT, 3, GL_DYNAMIC_DRAW);
  vbo.Upload(pc_s.ptr_, pc_s.SizeBytes(), 0);
  vbo_t.Reinitialise(pangolin::GlArrayBuffer, pc_t.Area(),  GL_FLOAT, 3, GL_DYNAMIC_DRAW);
  vbo_t.Upload(pc_t.ptr_, pc_t.SizeBytes(), 0);
  vbo_c.Reinitialise(pangolin::GlArrayBuffer, pc_mtx.Area(),  GL_FLOAT, 3, GL_DYNAMIC_DRAW);
  vbo_c.Upload(pc_mtx.ptr_, pc_mtx.SizeBytes(), 0);

  Eigen::SparseMatrix<float> L_s(pc_s.Area(), pc_s.Area());
  Eigen::SparseMatrix<float> L_t(pc_t.Area(), pc_t.Area());
  Eigen::MatrixXf S_wl(L_s.rows(),(int)numEv);//cols are evectors
  Eigen::MatrixXf T_wl(L_t.rows(),(int)numEv);
  Eigen::VectorXf evector_s(L_s.rows());
  Eigen::VectorXf evector_t(L_t.rows());
  tdp::eigen_vector<tdp::Vector3fda> means_s(nBins, tdp::Vector3fda(0,0,0));
  tdp::eigen_vector<tdp::Vector3fda> means_t(nBins, tdp::Vector3fda(0,0,0));

  // Stream and display video
  while(!pangolin::ShouldQuit())
  {
    // clear the OpenGL render buffers
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glColor3f(1.0f, 1.0f, 1.0f);

    if (pangolin::Pushed(showFMap) || knn.GuiChanged() || alpha.GuiChanged() ||
            numEv.GuiChanged() || nBins.GuiChanged()){
      std::cout << "Running fMap..." << std::endl;

      // get Laplacian operator and its eigenvectors
      tdp::Timer t0;
      L_s = tdp::getLaplacian(pc_s, ann_s, knn, eps, alpha);
      L_t = tdp::getLaplacian(pc_t, ann_t, knn, eps, alpha);
      t0.toctic("GetLaplacians");

      tdp::getLaplacianBasis(L_s, numEv, S_wl);
      evector_s = S_wl.col(1); // first non-trivial evector
      means_s = tdp::getLevelSetMeans(pc_s, evector_s, nBins); //means based on the evector_s's nBins level sets

      tdp::getLaplacianBasis(L_t, numEv, T_wl);
      evector_t = T_wl.col(1); // first non-trivial evector
      means_t = tdp::getLevelSetMeans(pc_t, evector_t, nBins);
      t0.toctic("GetEigenVectors & GetMeans");

      // color-coding on the surface
      valuebo_s.Reinitialise(pangolin::GlArrayBuffer, evector_s.rows(),GL_FLOAT,1, GL_DYNAMIC_DRAW);
      valuebo_s.Upload(&evector_s(0), sizeof(float)*evector_s.rows(), 0);
      std::cout << evector_s.minCoeff() << " " << evector_s.maxCoeff() << std::endl;
      minVal = evector_s.minCoeff()-1e-3;
      maxVal = evector_s.maxCoeff();

      valuebo_t.Reinitialise(pangolin::GlArrayBuffer, evector_t.rows(),GL_FLOAT,1, GL_DYNAMIC_DRAW);
      valuebo_t.Upload(&evector_t(0), sizeof(float)*evector_t.rows(), 0);
      std::cout << evector_t.minCoeff() << " " << evector_t.maxCoeff() << std::endl;
      minVal_t = evector_t.minCoeff()-1e-3;
      maxVal_t = evector_t.maxCoeff();


      //--playing around here
      int numCst = 2*(int)numEv;
      tdp::Vector3fda mean_s, mean_t;
      Eigen::VectorXf f_w(pc_s.Area()), g_w(pc_t.Area());
      Eigen::VectorXf f_l((int)numEv), g_l((int)numEv);
      Eigen::MatrixXf F(numCst, (int)numEv), G(numCst, (int)numEv);
      Eigen::MatrixXf C((int)numEv, (int)numEv);
      // solve least-square
      Eigen::FullPivLU<Eigen::MatrixXf> B_lu;


      // construct F(design matrix) using point correspondences
      Eigen::VectorXi nnIds(1);
      Eigen::VectorXf dists(1);
      for (int i=0; i< (int)numEv; ++i){
          ann_t.Search(pc_s[i], 1, 1e-9, nnIds, dists);
          //std::cout << "match idx: " << i << ", " << nnIds(0) << std::endl;
          tdp::f_rbf(pc_s, pc_s[i], alpha, f_w); //todo: check if I can use this same alpha?
          tdp::f_rbf(pc_t, pc_t[nnIds(0)], alpha, g_w);
          //std::cout << "f_w: " /*<< f_w.transpose() */<< std::endl;
          //std::cout << "g_w: " /*<< g_w.transpose()*/ << std::endl;

          f_l = (S_wl.transpose()*S_wl).fullPivLu().solve(S_wl.transpose()*f_w);
          //std::cout << "f_l: " << f_l.transpose() << std::endl;
          g_l = (T_wl.transpose()*T_wl).fullPivLu().solve(T_wl.transpose()*g_w);
          //std::cout << "g_l: " << g_l.transpose() << std::endl;
          F.row(i) = f_l;
          G.row(i) = g_l;
      }

      // adds more constraints
      for (int i=(int)numEv; i< numCst; ++i){
          ann_t.Search(pc_s[i], 1, 1e-9, nnIds, dists);
          //std::cout << "match idx: " << i << ", " << nnIds(0) << std::endl;
          tdp::f_rbf(pc_s, pc_s[i], alpha, f_w); //todo: check if I can use this same alpha?
          tdp::f_rbf(pc_t, pc_t[nnIds(0)], alpha, g_w);
          //std::cout << "f_w: " /*<< f_w.transpose() */<< std::endl;
          //std::cout << "g_w: " /*<< g_w.transpose()*/ << std::endl;

          f_l = (S_wl.transpose()*S_wl).fullPivLu().solve(S_wl.transpose()*f_w);
          //std::cout << "f_l: " << f_l.transpose() << std::endl;
          g_l = (T_wl.transpose()*T_wl).fullPivLu().solve(T_wl.transpose()*g_w);
          //std::cout << "g_l: " << g_l.transpose() << std::endl;
          F.row(i) = f_l;
          G.row(i) = g_l;
      }

      // solve least-square
      C = (F.transpose()*F).fullPivLu().solve(F.transpose()*G);
      //std::cout << "F: \n" << F.rows() << F.cols() << std::endl;
      //std::cout << "\nG: \n" << G.rows() << G.cols() << std::endl;
      //std::cout << "\nC: \n" << C << /*C.rows() << C.cols() <<*/ std::endl;

      // Get the point-wise correspondence
      int x = 0;
      std::cout << "query: \n" << pc_s[x]<<std::endl;
      int y = getCorrespondence(pc_s, pc_t, S_wl, T_wl, C, alpha, x);
      ann_t.Search(pc_s[x], 1, 1e-9, nnIds, dists);
      std::cout << "guess: \n" << pc_t[y] << std::endl;
      std::cout << "true: \n" << nnIds(0) << std::endl;


      //color coding of the C matrix
      Eigen::VectorXf cvec(pc_mtx.Area());
      for (int r=0; r<C.rows(); ++r){
          for (int c=0; c<C.cols(); ++c){
              cvec(r*C.cols()+c) = C(r,c);
          }
      }

      valuebo_c.Reinitialise(pangolin::GlArrayBuffer, cvec.rows(), GL_FLOAT,1, GL_DYNAMIC_DRAW);
      valuebo_c.Upload(&cvec(0), sizeof(float)*cvec.rows(), 0);
      std::cout << cvec.minCoeff() << " " << cvec.maxCoeff() << std::endl;
      minVal_c = cvec.minCoeff()-1e-3;
      maxVal_c = cvec.maxCoeff();

      //TODO: segment correspondence
      //    : operator commutativity constraint?
      //    : any other constraint to add to the least square?
      //TODO: add a handler to choose correponding points from each point cloud by clicking on the gui
      //    : use the points to construct least-square
      //    : check how well that works
      //START HERE!



      //--end of playing
      std::cout << "<--DONE fMap-->" << std::endl;
    }
    // Draw 3D stuff
    glEnable(GL_DEPTH_TEST);
    glColor3f(1.0f, 1.0f, 1.0f);
    if (viewPc.IsShown()) {
      viewPc.Activate(s_cam);
      pangolin::glDrawAxis(0.1);

      // draw lines connecting the means
      glColor3f(.3,1.,.125);
      glLineWidth(2);
      tdp::Vector3fda m, m_prev;
      for (size_t i=1; i<means_s.size(); ++i){
          m_prev = means_s[i-1];
          m = means_s[i];
          tdp::glDrawLine(m_prev, m);
      }

      glPointSize(2.);
      glColor3f(1.0f, 1.0f, 0.0f);
      // renders the vbo with colors from valuebo
      auto& shader = tdp::Shaders::Instance()->valueShader_;
      shader.Bind();
      shader.SetUniform("P",  s_cam.GetProjectionMatrix());
      shader.SetUniform("MV", s_cam.GetModelViewMatrix());
      shader.SetUniform("minValue", minVal);
      shader.SetUniform("maxValue", maxVal);
      valuebo_s.Bind();
      glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, 0);
      vbo.Bind();
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

      glEnableVertexAttribArray(0);
      glEnableVertexAttribArray(1);
      glPointSize(4.);
      glDrawArrays(GL_POINTS, 0, vbo.num_elements);
      shader.Unbind();
      glDisableVertexAttribArray(1);
      valuebo_s.Unbind();
      glDisableVertexAttribArray(0);
      vbo.Unbind();
    }

    if (viewPc_t.IsShown()){
        viewPc_t.Activate(t_cam);
        pangolin::glDrawAxis(0.1);

        // draw lines connecting the means
        glColor3f(.3,1.,.125);
        glLineWidth(2);
        tdp::Vector3fda m, m_prev;
        for (size_t i=1; i<means_t.size(); ++i){
            m_prev = means_t[i-1];
            m = means_t[i];
            tdp::glDrawLine(m_prev, m);
        }

        glPointSize(2.);
        glColor3f(1.0f, 1.0f, 0.0f);

        // renders the vbo with colors from valuebo
        auto& shader_t = tdp::Shaders::Instance()->valueShader_;
        shader_t.Bind();
        shader_t.SetUniform("P",  t_cam.GetProjectionMatrix());
        shader_t.SetUniform("MV", t_cam.GetModelViewMatrix());
        shader_t.SetUniform("minValue", minVal_t);
        shader_t.SetUniform("maxValue", maxVal_t);
        valuebo_t.Bind();
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, 0);
        vbo_t.Bind();
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glPointSize(4.);
        glDrawArrays(GL_POINTS, 0, vbo_t.num_elements);
        shader_t.Unbind();
        glDisableVertexAttribArray(1);
        valuebo_t.Unbind();
        glDisableVertexAttribArray(0);
        vbo_t.Unbind();
    }

    if (viewMtx.IsShown()){
        viewMtx.Activate(mtx_cam);
        pangolin::glDrawAxis(0.1);

        // plots dots with the same number of rows and cols of C
        glPointSize(5.);
        glColor3f(1.0f, 1.0f, 0.0f);

        // renders the vbo with colors from valuebo
        auto& shader = tdp::Shaders::Instance()->valueShader_;
        shader.Bind();
        shader.SetUniform("P",  mtx_cam.GetProjectionMatrix());
        shader.SetUniform("MV", mtx_cam.GetModelViewMatrix());
        shader.SetUniform("minValue", minVal_c);
        shader.SetUniform("maxValue", maxVal_c);
        valuebo_c.Bind();
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, 0);
        vbo_c.Bind();
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glPointSize(4.);
        glDrawArrays(GL_POINTS, 0, vbo_c.num_elements);
        shader.Unbind();
        glDisableVertexAttribArray(1);
        valuebo_c.Unbind();
        glDisableVertexAttribArray(0);
        vbo_c.Unbind();

    }
    glDisable(GL_DEPTH_TEST);
    // leave in pixel orthographic for slider to render.
    pangolin::DisplayBase().ActivatePixelOrthographic();
    // finish this frame
    pangolin::FinishFrame();
  }

  std::cout << "good morning!" << std::endl;
  return 0;
}