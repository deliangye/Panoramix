#include "../src/core/mesh_maker.hpp"
#include "../src/core/utilities.hpp"
#include "../src/core/views_net.hpp"
#include "../src/vis/views_net_visualize.hpp"
#include "../src/vis/regions_net_visualize.hpp"
#include "gtest/gtest.h"

#include <iostream>
#include <string>
#include <random>


using namespace panoramix;

// PROJECT_TEST_DATA_DIR_STR is predefined using CMake
static const std::string ProjectTestDataDirStr = PROJECT_TEST_DATA_DIR_STR;

TEST(ViewsNet, FixedCamera) {

    cv::Mat panorama = cv::imread(ProjectTestDataDirStr + "/13.jpg");
    cv::resize(panorama, panorama, cv::Size(2000, 1000));
    core::PanoramicCamera originCam(panorama.cols / M_PI / 2.0);

    std::vector<core::PerspectiveCamera> cams;
    core::Mesh<core::Vec3> cameraStand;
    core::MakeQuadFacedSphere(cameraStand, 6, 12);
    for (auto & v : cameraStand.vertices()){
        core::Vec3 direction = v.data;
        if (core::AngleBetweenDirections(direction, core::Vec3(0, 0, 1)) <= 0.1 ||
            core::AngleBetweenDirections(direction, core::Vec3(0, 0, -1)) <= 0.1){
            //cams.emplace_back(700, 700, originCam.focal(), core::Vec3(0, 0, 0), direction, core::Vec3(0, 1, 0));
            continue;
        }
        else{
            cams.emplace_back(700, 700, originCam.focal(), core::Vec3(0, 0, 0), direction, core::Vec3(0, 0, -1));
        }
    }
    std::random_shuffle(cams.begin(), cams.end());


    /// insert into views net
    core::ViewsNet::Params params;
    params.mjWeightT = 2.0;
    params.intersectionConstraintLineDistanceAngleThreshold = 0.05;
    params.incidenceConstraintLineDistanceAngleThreshold = 0.2;
    params.mergeLineDistanceAngleThreshold = 0.05;
    core::ViewsNet net(params);

    for (int i = 0; i < cams.size(); i++){
        std::cout << "photo: " << i << std::endl;

        auto & camera = cams[i];
        const auto im = 
            core::CameraSampler<core::PerspectiveCamera, core::PanoramicCamera>(camera, originCam)(panorama);
        auto viewHandle = net.insertPhoto(im, camera);

        std::cout << "extracting features ...";

        net.computeFeatures(viewHandle);

        vis::Visualizer2D(im)
            << vis::manip2d::SetColor(core::Color(0, 0, 255))
            << vis::manip2d::SetThickness(2)
            << net.views().data(viewHandle).lineSegments
            << vis::manip2d::SetColor(core::Color(255, 0, 0))
            << vis::manip2d::SetThickness(1)
            << net.views().data(viewHandle).lineSegmentIntersections
            << vis::manip2d::Show();

        net.updateConnections(viewHandle);
        net.calibrateCamera(viewHandle);
        net.calibrateAllCameras();

        if (net.isTooCloseToAnyExistingView(viewHandle).isValid()){
            std::cout << "too close to existing view, skipped";
            continue;
        }

        std::cout << "calibrating camera and classifying lines ...";

        // estimate vanishing points and classify lines
        net.estimateVanishingPointsAndClassifyLines();
        auto vps = net.globalData().vanishingPoints;
        for (auto & vp : vps)
            vp /= core::norm(vp);
        double ortho = core::norm(core::Vec3(vps[0].dot(vps[1]), vps[1].dot(vps[2]), vps[2].dot(vps[0])));
        EXPECT_LT(ortho, 1e-1);

        auto antivps = vps;
        for (auto & p : antivps)
            p = -p;

        std::vector<core::Vec3> allvps(vps.begin(), vps.end());
        allvps.insert(allvps.end(), antivps.begin(), antivps.end());

        std::vector<core::Point2> vp2s(allvps.size());
        std::transform(allvps.begin(), allvps.end(), vp2s.begin(),
            [&originCam](const core::Vec3 & p3){
            return originCam.screenProjection(p3);
        });

        vis::Visualizer2D()
            << net.views().data(viewHandle)
            << vis::manip2d::Show();

        vis::Visualizer3D() << vis::manip3d::SetCamera(core::PerspectiveCamera(700, 700, 200, core::Vec3(1, 1, 1) / 4, core::Vec3(0, 0, 0), core::Vec3(0, 0, -1)))
            << vis::manip3d::SetBackgroundColor(core::ColorTag::Black)
            << vis::manip3d::SetColorTableDescriptor(core::ColorTableDescriptor::RGB)
            << net.globalData().spatialLineSegments
            << vis::manip3d::AutoSetCamera
            << vis::manip3d::SetRenderMode(vis::RenderModeFlag::All)
            << vis::manip3d::Show(false);

        net.rectifySpatialLines();

        vis::Visualizer3D() << vis::manip3d::SetCamera(core::PerspectiveCamera(700, 700, 200, core::Vec3(1, 1, 1) / 4, core::Vec3(0, 0, 0), core::Vec3(0, 0, -1)))
            << vis::manip3d::SetBackgroundColor(core::ColorTag::Black)
            << vis::manip3d::SetColorTableDescriptor(core::ColorTableDescriptor::RGB)
            << net.globalData().mergedSpatialLineSegments
            << vis::manip3d::AutoSetCamera
            << vis::manip3d::SetRenderMode(vis::RenderModeFlag::All)
            << vis::manip3d::Show(false);

        vis::Visualizer3D() << vis::manip3d::SetCamera(core::PerspectiveCamera(700, 700, 200, core::Vec3(1, 1, 1) / 4, core::Vec3(0, 0, 0), core::Vec3(0, 0, -1)))
            << vis::manip3d::SetBackgroundColor(core::ColorTag::Black)
            << vis::manip3d::SetColorTableDescriptor(core::ColorTableDescriptor::RGB)
            << net.globalData()
            << vis::manip3d::AutoSetCamera
            << vis::manip3d::SetRenderMode(vis::RenderModeFlag::All)
            << vis::manip3d::Show();
    }

}



int main(int argc, char * argv[], char * envp[])
{
    srand(clock());
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
