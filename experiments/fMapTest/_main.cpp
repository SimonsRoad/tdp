#include <iostream>
#include <cmath>
#include <complex>
#include <vector>
#include <string>
#include <sstream>
#include <cstdlib>
#include <random>

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
#include <Eigen/Core>

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
#include <tdp/eigen/dense_io.h>
#include <tdp/eigen/sparse_io.h>

#include <unistd.h> //check if cached files exist

#include <tdp/laplace_beltrami/laplace_beltrami.h>


/************TODO***********************************************/
/***************************************************************/
//1. MOVE THE TESTS TO A SEPARATE TEST FILE
//2. FOCUS ON arm.ply and bunny ply

/************Declarations***************************************
 ***************************************************************/
void Test_samePc_sameSamples(
        int nSamples, 
        int nEv,
        int nTrain,
        std::string& option,
        int shapeOpt,
        bool showDecomposition = false);

void Test_samePc_diffSamples(
        int nSamples, 
        // std::string option = std::string("rbf"), 
        std::string& option,
        bool showDecomposition = false);


int main(int argc, char* argv[]){
  //Create openGL window - guess sensible dimensions
  int menu_w = 180;
  pangolin::CreateWindowAndBind("GuiBase", 1200+menu_w, 800);
  //Current frame in memory buffer and displayingh
  pangolin::CreatePanel("ui").SetBounds(0.,1.,0.,pangolin::Attach::Pix(menu_w));
  // Assume packed OpenGL data unless otherwise specified
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  //Setup container
  pangolin::View& container = pangolin::Display("container");
  container.SetLayout(pangolin::LayoutEqual)
      .SetBounds(0.,1.0, pangolin::Attach::Pix(menu_w), 1.0);
  //Define Camera Render Object (for view/scene browsing)
  pangolin::OpenGlRenderState s_cam(
    pangolin::ProjectionMatrix(640,480,420,420,320,240,0.1,1000),
    pangolin::ModelViewLookAt(0,0.5,-3, 0,0,0, pangolin::AxisNegY)
    );
  pangolin::OpenGlRenderState t_cam(
    pangolin::ProjectionMatrix(640,480,420,420,320,240,0.1,1000),
    pangolin::ModelViewLookAt(0,0.5,-3, 0,0,0, pangolin::AxisNegY)
    );
  pangolin::OpenGlRenderState cmtx_cam(
    pangolin::ProjectionMatrix(640,480,420,420,320,240,0.1,1000),
    pangolin::ModelViewLookAt(0,0.5,-3, 0,0,0, pangolin::AxisNegY)
    );

  // Add named OpenGL viewport to window and provide 3D Handler
  pangolin::View& view_s = pangolin::CreateDisplay()
                          .SetHandler(new pangolin::Handler3D(s_cam));
  pangolin::View& view_t = pangolin::CreateDisplay()
                          .SetHandler(new pangolin::Handler3D(t_cam));
  pangolin::View& view_cmtx = pangolin::CreateDisplay()
                          .SetHandler(new pangolin::Handler3D(cmtx_cam));
  container.AddDisplay(view_s);
  container.AddDisplay(view_t);
  container.AddDisplay(view_cmtx);
    
  // Add variables to pangolin GUI
  pangolin::Var<bool> showFMap("ui.show fMap", true, false);
  pangolin::Var<bool> showMeans("ui.show means", true, false);
  pangolin::Var<bool> showFTransfer(" ui. show fTransfer", true, true);
  pangolin::Var<bool> showDecomposition("ui. show evals", false, true);

  pangolin::Var<int> nSamples("ui. num samples from mesh pc", 400, 100, 400);

  pangolin::Var<int> shapeOpt("ui. shape option", 2, 0, 3); //2:bunny
  //-- variables for KNN
  pangolin::Var<int> knn("ui.knn",30,1,100);//(int)nSamples
  pangolin::Var<float> eps("ui.eps", 1e-6 ,1e-7, 1e-5);
  pangolin::Var<float> alpha("ui. alpha", 0.01, 0.001, 0.3); //variance of rbf kernel for laplacian
  pangolin::Var<float> alpha2("ui. alpha2", 0.001, 0.001, 0.5); //variance of rbf kernel for defining function on manifold

  //--Correspondence Matrix C estimation
  pangolin::Var<int> nEv("ui.numEv",50,30,100); //min=1, max=pc_s.Area()
  pangolin::Var<int> nCst("ui.numCst",nEv/*std::min(20*numEv, pc_s.Area())*/,nEv,nSamples);
  pangolin::Var<int> nSteps("ui.nSteps for HKS", 100, 50,300); //number of timesteps for HKS
  //-- viz color coding
  pangolin::Var<float>minVal("ui. min Val",-0.71,-1,0);
  pangolin::Var<float>maxVal("ui. max Val",0.01,1,0);
  pangolin::Var<int>nQ("ui. num Queries", 100, 100, nSamples);
  float minVal_t, maxVal_t, minVal_c, maxVal_c, minCValue, maxCValue,
          minF0Value, maxF0Value, minG0Value, maxG0Value;

  //End of Pangolin GUI setup

  std::cout << "Test Correspondences---" << std::endl;
  std::string option("rbf");
  // int shapeOpt = 2;
  // int nSamples = 400;
  // int nEv = 50;
  int nTrain = nCst;
  // bool showDecomposition = false;
  // nTrain = nSamples - 50;
  Test_samePc_sameSamples(nSamples,nEv, nTrain,
                          option,
                          shapeOpt,
                          showDecomposition);
  // for (int i=0; i< floor(nSamples/nEv); ++i){
  //     nTrain = i*nEv;
  //     std::cout << "\n================================="<< std::endl;
  //     std::cout <<"nPoints: " << nSamples <<", nEv: " << nEv << ", nPW corresp.: "
  //               << nTrain << std::endl;
  //     Test_samePc_sameSamples(nSamples,nEv, nTrain,
  //                           option, shapeOpt, showDecomposition);
  //     std::cout << "=================================\n"<< std::endl;

  //  }

  //todo: time to incorporate this with the fmap main.cpp so that
  //    we don't recalcualte repeated things (such as Laplacian)
  //run the experiement with the pipe to a log file

  // Test_samePc_diffSamples(option);
}


// void Test_samePc_withGNoise
void Test_samePc_sameSamples(int nSamples, int nEv, int nPW,
                             std::string& option,
                             int shapeOpt,
                             bool showDecomposition){
  /* nSamples: number of points on the surface
   * nTrain: number of  correpondence pairs given to approximate functional mapping
   *  - must be at least numEv, and at most nSamples
   * option: "rbf" or "ind".  function to construct a function on the surface based on a given point
   * shapeOpt: 0 is "linear" for simplePc,
   *         : 1 is "sphere" for random sampling from unit sphere
   *         : 2 is "bunny",
   *         : 3 is "manekine"
   * showDecompotion: true to see (eigenfunctions and) eigenvalues of the Laplacian of both surfaces
   */

//todo: std::option
    tdp::ManagedHostImage<tdp::Vector3fda> pc_all;
    tdp::ManagedHostImage<tdp::Vector3fda> pc_s;
    tdp::ManagedHostImage<tdp::Vector3fda> pc_t;
    std::string fpath;

    std::cout << "shape opt: " << shapeOpt << std::endl;

    // Get point clouds
    switch (shapeOpt){
      case 0:
        tdp::GetSimplePc(pc_s, nSamples);
        std::cout << "Shape: linear---" << std::endl;
        break;
      case 1:
        tdp::GetSphericalPc(pc_s, nSamples);
        std::cout << "Shape: sphere---" << std::endl;
        break;
      case 2:
        fpath = std::string("/home/hjsong/workspace/data/mesh/bun_zipper_res4.ply"); //todo: hardcoded
        tdp::LoadPointCloudFromMesh(fpath, pc_all);
        tdp::GetSamples(pc_all, pc_s, nSamples);
        std::cout << "Shape: bunny---" << std::endl;
        break;
      case 3:
        tdp::ManagedHostImage<tdp::Vector3fda> pc_all;
        fpath = std::string("/home/hjsong/workspace/data/mesh/cleanCylinder_0.ply"); //todo: hardcoded
        tdp::LoadPointCloudFromMesh(fpath, pc_all);
        tdp::GetSamples(pc_all, pc_s, nSamples);
        std::cout << "Shape: manekine---" << std::endl;
    }

    pc_t.ResizeCopyFrom(pc_s);

    //tdp::print("PC_S---");
    //tdp::printImage(pc_s, 0, pc_s.Area());
    //tdp::print("PC_T---");
    //tdp::printImage(pc_t, 0, pc_t.Area());

    /****parameters*****/
    /*******************/
    //int nEv = std::min(30, (int)(pc_s.Area()/3));//pc_s.Area()-2;
    //get ALL eigenvectors of L
    int knn = pc_s.Area(); // use all points as neighbors
    float eps = 1e-6;
    float alpha = 0.01;
    float alpha2 = 0.1;

    //int nPW = nTrain;//nEv;//number of pointwise correspondences
    int nHKS = 0; //number of heat kernel signature correspondences
    int nCst = nPW + nHKS;//pc_s.Area();
    
    //int nTest = pc_s.Area() - nPW;
    int nShow = std::min(10, nEv);
    /*******************/

    // build kd tree
    tdp::ANN ann_s, ann_t;
    ann_s.ComputeKDtree(pc_s);
    ann_t.ComputeKDtree(pc_t);

    // construct laplacian matrices
    Eigen::SparseMatrix<float> L_s(pc_s.Area(), pc_s.Area()),
                               L_t(pc_t.Area(), pc_t.Area());
    Eigen::MatrixXf S_wl(L_s.rows(),(int)nEv),//cols are evectors
                    T_wl(L_t.rows(),(int)nEv),
                    S_desc_w, T_desc_w,
                    S_desc_l, T_desc_l;
    Eigen::VectorXf S_evals((int)nEv), T_evals((int)nEv);

    //******************CACHE NAMING*************************//
    //*******************************************************//
    std::stringstream ss;
    std::string _path_ls, _path_lt, _path_s_wl, _path_t_wl, 
                _path_s_evals, _path_t_evals;
    ss << "./cache/" << shapeOpt << "/ls_" << nSamples << "_" << knn << "_"
        << alpha << ".dat";
    _path_ls = ss.str(); 
    ss.str(std::string());

    ss << "./cache/" << shapeOpt << "/lt_" << nSamples << "_" << knn << "_"
        << alpha << ".dat";
    _path_lt = ss.str(); 
    ss.str(std::string());

    ss << "./cache/" << shapeOpt << "/s_wl_" << nSamples << "_" << nEv
       << ".dat";
    _path_s_wl = ss.str(); 
    ss.str(std::string());

    ss << "./cache/" << shapeOpt << "/t_wl_" << nSamples << "_" << nEv
       << ".dat";
    _path_t_wl = ss.str(); 
    ss.str(std::string());

    ss << "./cache/" << shapeOpt << "/s_evals_" << nSamples << "_" << nEv
       << ".dat";
    _path_s_evals = ss.str(); 
    ss.str(std::string());

    ss << "./cache/" << shapeOpt << "/t_evals_" << nSamples << "_" << nEv
       << ".dat";
    _path_t_evals = ss.str(); 
    ss.str(std::string());


    std::cout << _path_ls << std::endl;
    std::cout << _path_lt << std::endl;
    std::cout << _path_s_wl << std::endl;
    std::cout << _path_t_wl << std::endl;
    std::cout << _path_s_evals << std::endl;
    std::cout << _path_t_evals << std::endl;

//todo: clean this up
    const char* path_ls = _path_ls.c_str();
    const char* path_lt = _path_lt.c_str();
    const char* path_s_wl = _path_s_wl.c_str();
    const char* path_t_wl = _path_t_wl.c_str();
    const char* path_s_evals = _path_s_evals.c_str();
    const char* path_t_evals = _path_t_evals.c_str();


    int res = access(path_ls, R_OK) 
                + access(path_lt, R_OK);


    if (res == 0){
        // Read cached file
        std::cout << "Reading Laplacians from cache---" << std::endl;
        
        tdp::read_binary(path_ls, L_s);
        tdp::read_binary(path_lt, L_t);
    } else{
        L_s = tdp::getLaplacian(pc_s, ann_s, knn, eps, alpha);
        L_t = tdp::getLaplacian(pc_t, ann_t, knn, eps, alpha);

        tdp::write_binary(path_ls, L_s);
        tdp::write_binary(path_lt, L_t);
    
        std::cout << "Cached: Laplacians" << std::endl;
    }

    res = access(path_s_wl, R_OK) 
                + access(path_t_wl, R_OK)
                + access(path_s_evals, R_OK)
                + access(path_t_evals, R_OK);

    if (res == 0){    
        std::cout << "Reading Bases&evals from cache---" << std::endl;
        tdp::read_binary(path_s_wl, S_wl);
        tdp::read_binary(path_t_wl, T_wl);
        tdp::read_binary(path_s_evals, S_evals);
        tdp::read_binary(path_t_evals, T_evals);

    } else{
        std::cout << "Calculating Bases&evals---" << std::endl;
        tdp::decomposeLaplacian(L_s, nEv, S_evals, S_wl); //todo: check if size initialization is included
        tdp::decomposeLaplacian(L_t, nEv, T_evals, T_wl);

        tdp::write_binary(path_s_wl, S_wl);
        tdp::write_binary(path_t_wl, T_wl);
        tdp::write_binary(path_s_evals, S_evals);
        tdp::write_binary(path_t_evals, T_evals);
        std::cout << "Cached: bases, evals" << std::endl;

    }

    if (showDecomposition){
        // std::cout << "Basis ---" << std::endl;
        // std::cout << "n of evec: " << nEv << std::endl;
        // std::cout << S_wl << std::endl;
        std::cout << "-----------------" << std::endl;
        std::cout << T_wl << std::endl;
        std::cout << "Evals ---" << std::endl;
        std::cout << "\tS: " << S_evals.transpose() << std::endl;
        std::cout << "\tT: " << T_evals.transpose() << std::endl;
    }

    //--Construct function pairs
    Eigen::VectorXf f_w(pc_s.Area()), g_w(pc_t.Area()),
                    f_l((int)nEv), g_l((int)nEv);
    Eigen::MatrixXf F((int)nCst, (int)nEv), G((int)nCst, (int)nEv);
    Eigen::MatrixXf C((int)nEv, (int)nEv);

    // --construct F(data matrix) and G based on the correspondences

    for (int i=0; i< (int)nPW; ++i){
        tdp::f_landmark(pc_s, i, alpha2, option, f_w);
        tdp::f_landmark(pc_t, i, alpha2, option, g_w);

        f_l = (S_wl.transpose()*S_wl).fullPivLu().solve(S_wl.transpose()*f_w);
        g_l = (T_wl.transpose()*T_wl).fullPivLu().solve(T_wl.transpose()*g_w);
        //f_l = tdp::projectToLocal(S_wl, f_w);
        //g_l = tdp::projectToLocal(T_wl, g_w);

        F.row(i) = f_l;
        G.row(i) = g_l;
    }


    if (nHKS >0){
        //-----Add  heat kernel signatures as constraints
        std::cout << "CALCULATEING HKS ---" <<std::endl;
        S_desc_w = tdp::getHKS(S_wl,S_evals,nHKS);
        T_desc_w = tdp::getHKS(T_wl,T_evals,nHKS);
        S_desc_l = (S_wl.transpose()*S_wl).fullPivLu().solve(S_wl.transpose()*S_desc_w);
        T_desc_l = (T_wl.transpose()*T_wl).fullPivLu().solve(T_wl.transpose()*T_desc_w);
        //S_desc_l = tdp::projectToLocal(S_wl, S_desc_w); //columne is a feature
        //T_desc_l = tdp::projectToLocal(T_wl, T_desc_w);
        
        assert(S_desc_l.cols() == nHKS);
        for (int i=0; i<nHKS; ++i){
          F.row(nPW+i) = S_desc_l.col(i);
          G.row(nPW+i) = T_desc_l.col(i);
        }
        std::cout << "S,T descriptors at time 0--------" << std::endl;
        std::cout << S_desc_l.col(0) << std::endl;//heat kernel at timestap i//todo: check at which point for S and T manifolds
        std::cout << T_desc_l.col(0) << std::endl;//heat kernel at timestap i
    }
    //----Add operator constratins
    //

    // solve least-square
    C = (F.transpose()*F).fullPivLu().solve(F.transpose()*G);
    //std::cout << "F: \n" << F.rows() << F.cols() << std::endl;
    //std::cout << "\nG: \n" << G.rows() << G.cols() << std::endl;

    std::cout << "-----------\n"
              << "C(10x10) \n" 
              << C.block(0,0,nShow,nShow) 
              << std::endl;

    std::cout << "----------\n"
              << "Diagnoals\n"
              << C.diagonal().transpose() 
              << std::endl;

    // Test
    assert(nPW < pc_s.Area());
    int nTest = (int)pc_s.Area()-nPW;
    float error = 0;
    Eigen::VectorXf true_w, true_l, guess_w;
    for (int i=nPW; i< (int)pc_s.Area(); ++i ){
        tdp::f_landmark(pc_s, i, alpha2, option, true_w);
        true_l = (S_wl.transpose()*S_wl).fullPivLu().solve(S_wl.transpose()*true_w);
        guess_w = S_wl * (C*true_l);
        // tdp::Vector3fda true_l = tdp::projectToLocal(S_wl, true_w);
        // tdp::Vector3fda guess_w = tdp::projectToWorld(S_wl, C*true_l);
        error += (true_w - guess_w).squaredNorm(); //todo: 
    }
    error = std::sqrt(error/nTest); //rms
    std::cout << "Surface dim: " << pc_s.Area() << std::endl;
    std::cout << "N test points: " << nTest << std::endl;
    std::cout << "rms: " << error << std::endl;

}

void Test_samePc_diffSamples(int nSamples, std::string& option, bool showDecomposition){
//todo: std::option
    //todo: think about how to get the correspondances - sort?
    //    : change seed, get the two different spheres 

    tdp::ManagedHostImage<tdp::Vector3fda> pc_s = tdp::GetSimplePc();
    //tdp::ManagedHostImage<tdp::Vector3fda> pc_s;
    //    tdp::GetSphericalPc(pc_s, nSamples);

    tdp::ManagedHostImage<tdp::Vector3fda> pc_t;
    addGaussianNoise(pc_s, 0.1f, pc_t);

    int nEv = std::min(30, (int)(pc_s.Area()/2));//pc_s.Area()-2; //get ALL eigenvectors of L
    int knn = pc_s.Area(); // use all points as neighbors
    float eps = 1e-6;
    float alpha = 0.01; // param for calculating Laplacian of a surface
    float alpha2 = 0.1; // param for radial basis function construction

    int nPW = nEv;//nber of pointwise correspondences
    int nHKS = 0; //nber of heat kernel signature correspondences
    int nCst = nPW + nHKS;//pc_s.Area();
    
    //int nTest = pc_s.Area() - nPW;

    // build kd tree
    tdp::ANN ann_s, ann_t;
    ann_s.ComputeKDtree(pc_s);
    ann_t.ComputeKDtree(pc_t);

    // construct laplacian matrices
    Eigen::SparseMatrix<float> L_s(pc_s.Area(), pc_s.Area()),
                               L_t(pc_t.Area(), pc_t.Area());
    Eigen::MatrixXf S_wl(L_s.rows(),(int)nEv),//cols are evectors
                    T_wl(L_t.rows(),(int)nEv),
                    S_desc_w, T_desc_w,
                    S_desc_l, T_desc_l;
    Eigen::VectorXf S_evals((int)nEv), T_evals((int)nEv);


    L_s = tdp::getLaplacian(pc_s, ann_s, knn, eps, alpha);
    L_t = tdp::getLaplacian(pc_t, ann_t, knn, eps, alpha);
    tdp::decomposeLaplacian(L_s, nEv, S_evals, S_wl); //todo: check if size initialization is included
    tdp::decomposeLaplacian(L_t, nEv, T_evals, T_wl);

    std::cout << "Basis ---" << std::endl;
    std::cout << "n of evec: " << nEv << std::endl;
    std::cout << S_wl << std::endl;
    std::cout << "-----------------" << std::endl;
    std::cout << T_wl << std::endl;
    std::cout << "Evals ---" << std::endl;
    std::cout << S_evals.transpose() << std::endl;
    std::cout << T_evals.transpose() << std::endl;


    //--Construct function pairs
    Eigen::VectorXf f_w(pc_s.Area()), g_w(pc_t.Area()),
                    f_l((int)nEv), g_l((int)nEv);
    Eigen::MatrixXf F((int)nCst, (int)nEv), G((int)nCst, (int)nEv);
    Eigen::MatrixXf C((int)nEv, (int)nEv);

    // --construct F(data matrix) and G based on the correspondences

    for (int i=0; i< (int)nPW; ++i){
        tdp::f_landmark(pc_s, i, alpha2, option, f_w);
        tdp::f_landmark(pc_t, i, alpha2, option, g_w);

        f_l = (S_wl.transpose()*S_wl).fullPivLu().solve(S_wl.transpose()*f_w);
        g_l = (T_wl.transpose()*T_wl).fullPivLu().solve(T_wl.transpose()*g_w);
        //f_l = tdp::projectToLocal(S_wl, f_w);
        //g_l = tdp::projectToLocal(T_wl, g_w);

        F.row(i) = f_l;
        G.row(i) = g_l;
    }


    if (nHKS >0){
        //-----Add  heat kernel signatures as constraints
        std::cout << "CALCULATEING HKS ---" <<std::endl;
        S_desc_w = tdp::getHKS(S_wl,S_evals,nHKS);
        T_desc_w = tdp::getHKS(T_wl,T_evals,nHKS);
        S_desc_l = (S_wl.transpose()*S_wl).fullPivLu().solve(S_wl.transpose()*S_desc_w);
        T_desc_l = (T_wl.transpose()*T_wl).fullPivLu().solve(T_wl.transpose()*T_desc_w);
        //S_desc_l = tdp::projectToLocal(S_wl, S_desc_w); //columne is a feature
        //T_desc_l = tdp::projectToLocal(T_wl, T_desc_w);
        
        assert(S_desc_l.cols() == nHKS);
        for (int i=0; i<nHKS; ++i){
          F.row(nPW+i) = S_desc_l.col(i);
          G.row(nPW+i) = T_desc_l.col(i);
        }
        std::cout << "S,T descriptors at time 0--------" << std::endl;
        std::cout << S_desc_l.col(0) << std::endl;//heat kernel at timestap i//todo: check at which point for S and T manifolds
        std::cout << T_desc_l.col(0) << std::endl;//heat kernel at timestap i
    }
    //----Add operator constratins
    //

    // solve least-square
    C = (F.transpose()*F).fullPivLu().solve(F.transpose()*G);
    //std::cout << "F: \n" << F.rows() << F.cols() << std::endl;
    //std::cout << "\nG: \n" << G.rows() << G.cols() << std::endl;
    std::cout << "\nC---------\n" << C << /*C.rows() << C.cols() <<*/ std::endl;

    // Test
    assert(nPW < pc_s.Area());
    int nTest = (int)pc_s.Area()-nPW;
    float error = 0;
    Eigen::VectorXf true_w, true_l, guess_w;
    for (int i=nPW; i< (int)pc_s.Area(); ++i ){
        tdp::f_landmark(pc_s, i, alpha2, option, true_w);
        true_l = (S_wl.transpose()*S_wl).fullPivLu().solve(S_wl.transpose()*true_w);
        guess_w = S_wl * (C*true_l);
        // tdp::Vector3fda true_l = tdp::projectToLocal(S_wl, true_w);
        // tdp::Vector3fda guess_w = tdp::projectToWorld(S_wl, C*true_l);
        error += (true_w - guess_w).squaredNorm();
    }
    error = std::sqrt(error/nTest);
    std::cout << "vector length: " << pc_s.Area() << std::endl;
    std::cout << "nber of test points: " << nTest << std::endl;
    std::cout << "error: " << error << std::endl;

}

