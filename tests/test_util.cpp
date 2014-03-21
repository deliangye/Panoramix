#include "../src/core/utilities.hpp"

#include <random>
#include <list>

#include <Eigen/Core>

#include "gtest/gtest.h"

using namespace panoramix;
using Mat4 = Eigen::Matrix4d;
using Vec3 = Eigen::Vector3d;
using Vec4 = Eigen::Vector4d;

TEST(UtilTest, WrapBetween) {
    for (int i = 0; i < 10000; i++){
        double x = double(rand()) / rand() + rand();
        double a = double(rand()) / rand() + rand();
        double b = a + abs(double(rand())/rand());
        if (a == b || std::isnan(x) || std::isnan(a) || std::isnan(b) || 
            std::isinf(x) || std::isinf(a) || std::isinf(b))
            continue;
        double xx = core::WrapBetween(x, a, b);
        double rem = (xx - x) / (b - a) - std::round((xx - x) / (b - a));
        if (std::isnan(rem)){
            assert(0);
        }
        ASSERT_NEAR(0, rem, 1e-5);
        ASSERT_LE(a, xx);
        ASSERT_LT(xx, b);
    }
}

TEST(UtilTest, MatrixLookAt) {
    Mat4 m;
    m.setIdentity();
    m = core::Matrix4MakeLookAt(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 0, 1), m);
    Vec4 p = m * Vec4(1, 0, 0, 1);
    Vec3 pj = Vec3(p(0), p(1), p(2)) / p(3);
    ASSERT_LT((pj - Vec3(0, 0, 1)).norm(), 2);
}

TEST(UtilTest, MergeNear) {
    std::list<double> arr1;
    arr1.resize(1000);
    std::generate(arr1.begin(), arr1.end(), std::rand);
    std::vector<double> arr2(arr1.begin(), arr1.end());

    double thres = 10;
    auto gBegins1 = core::MergeNear(std::begin(arr1), std::end(arr1), std::false_type(), thres);
    auto gBegins2 = core::MergeNear(std::begin(arr2), std::end(arr2), std::true_type(), thres);
    ASSERT_EQ(gBegins1.size(), gBegins2.size());
    auto i = gBegins1.begin();
    auto j = gBegins2.begin();
    for (; i != gBegins1.end(); ++i, ++j){
        EXPECT_EQ(**i, **j);
    }
    for (auto i = gBegins2.begin(); i != gBegins2.end(); ++i){
        auto inext = std::next(i);
        auto begin = *i;
        auto end = inext == gBegins2.end() ? std::end(arr2) : *inext;
        auto beginVal = *begin;
        for (auto j = begin; j != end; ++j){
            EXPECT_NEAR(*j, beginVal, thres);
        }
    }
}


int main(int argc, char * argv[], char * envp[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}