#include "../src/core/graphical_model.hpp"
#include "../src/core/mesh_maker.hpp"
#include "../src/vis/visualize3d.hpp"

#include "gtest/gtest.h"

using namespace panoramix;
using TestMesh = core::Mesh<core::Vec3>;


TEST(MiscToolsTest, ConditionalIterator) {

    std::list<int> ds;
    std::generate_n(std::back_inserter(ds), 100, std::rand);
    
    auto fun = [](int dd){return dd > 50; };
    
    std::vector<int> correct;
    std::copy_if(ds.begin(), ds.end(), std::back_inserter(correct), fun);
    
    auto correctIter = correct.begin();
    for (auto d : core::MakeConditionalContainer(&ds, fun)){
        ASSERT_EQ(*correctIter, d);
        ++correctIter;
    }

}

TEST(MeshTest, Conversion) {
    using CVMesh = core::Mesh<core::Point3>;
    CVMesh mesh;
    core::MakeQuadFacedCube(mesh);
    EXPECT_EQ(8, mesh.internalVertices().size());
    EXPECT_EQ(24, mesh.internalHalfEdges().size());
    EXPECT_EQ(6, mesh.internalFaces().size());

    std::vector<core::Line3> lines;
    std::vector<core::Point3> points;
    
    mesh.remove(CVMesh::VertHandle(0));

    for (auto h : mesh.halfedges()){
        auto p1 = mesh.data(h.topo.from());
        auto p2 = mesh.data(h.topo.to());
        lines.push_back({ p1, p2 });
    }
    for (auto v : mesh.vertices()){
        points.push_back(v.data);
    }

    vis::Visualizer3D()
        << vis::manip3d::SetColorTableDescriptor(vis::ColorTableDescriptor::RGB)
        << vis::manip3d::SetDefaultColor(vis::ColorTag::Black)
        << lines
        << vis::manip3d::SetDefaultColor(vis::ColorTag::Red)
        << vis::manip3d::SetPointSize(20)
        << points
        << vis::manip3d::SetCamera(core::PerspectiveCamera(500, 500, 500, 
            core::Vec3(-3, 0, 0), 
            core::Vec3(0.5, 0.5, 0.5)))
        << vis::manip3d::SetBackgroundColor(vis::ColorTag::White)
        << vis::manip3d::AutoSetCamera
        << vis::manip3d::Show();
}

TEST(MeshTest, Tetrahedron) {

    TestMesh mesh;
    core::MakeTetrahedron(mesh);
    EXPECT_EQ(4, mesh.internalVertices().size());
    EXPECT_EQ(12, mesh.internalHalfEdges().size());
    EXPECT_EQ(4, mesh.internalFaces().size());
    
    for (size_t i = 0; i < mesh.internalVertices().size(); i++) {
        TestMesh nmesh = mesh;
        nmesh.remove(TestMesh::VertHandle(i));
        nmesh.gc();
        
        EXPECT_EQ(3, nmesh.internalVertices().size());
        EXPECT_EQ(6, nmesh.internalHalfEdges().size());
        EXPECT_EQ(1, nmesh.internalFaces().size());
    }

    for (size_t i = 0; i < mesh.internalHalfEdges().size(); i++){
        TestMesh nmesh = mesh;
        nmesh.remove(TestMesh::HalfHandle(i));
        nmesh.gc();

        EXPECT_EQ(4, nmesh.internalVertices().size());
        EXPECT_EQ(10, nmesh.internalHalfEdges().size());
        EXPECT_EQ(2, nmesh.internalFaces().size());
    }

    for (size_t i = 0; i < mesh.internalFaces().size(); i++) {
        TestMesh nmesh = mesh;
        nmesh.remove(TestMesh::FaceHandle(i));
        nmesh.gc();

        EXPECT_EQ(4, nmesh.internalVertices().size());
        EXPECT_EQ(12, nmesh.internalHalfEdges().size());
        EXPECT_EQ(3, nmesh.internalFaces().size());
    }
    
}

TEST(MeshTest, Cube) {
    
    TestMesh mesh;
    core::MakeQuadFacedCube(mesh);
    EXPECT_EQ(8, mesh.internalVertices().size());
    EXPECT_EQ(24, mesh.internalHalfEdges().size());
    EXPECT_EQ(6, mesh.internalFaces().size());
    
    for (size_t i = 0; i < mesh.internalVertices().size(); i++) {
        TestMesh nmesh = mesh;
        nmesh.remove(TestMesh::VertHandle(i));
        nmesh.gc();
        
        EXPECT_EQ(7, nmesh.internalVertices().size());
        EXPECT_EQ(18, nmesh.internalHalfEdges().size());
        EXPECT_EQ(3, nmesh.internalFaces().size());
    }

    for (size_t i = 0; i < mesh.internalHalfEdges().size(); i++){
        TestMesh nmesh = mesh;
        nmesh.remove(TestMesh::HalfHandle(i));
        nmesh.gc();

        EXPECT_EQ(8, nmesh.internalVertices().size());
        EXPECT_EQ(22, nmesh.internalHalfEdges().size());
        EXPECT_EQ(4, nmesh.internalFaces().size());
    }

    for (size_t i = 0; i < mesh.internalFaces().size(); i++) {
        TestMesh nmesh = mesh;
        nmesh.remove(TestMesh::FaceHandle(i));
        nmesh.gc();

        EXPECT_EQ(8, nmesh.internalVertices().size());
        EXPECT_EQ(24, nmesh.internalHalfEdges().size());
        EXPECT_EQ(5, nmesh.internalFaces().size());
    }
    
}


TEST(ConstraintGraphTest, Basic) {
    
    using CGraph = core::ConstraintGraph<core::Dummy, core::Dummy>;

    CGraph cgraph;
    auto c0 = cgraph.addComponent();
    auto c1 = cgraph.addComponent();
    auto c2 = cgraph.addComponent();
    auto c3 = cgraph.addComponent();

    auto cc012 = cgraph.addConstraint({ c0, c1, c2 });
    auto cc123 = cgraph.addConstraint({ c1, c2, c3 });
    auto cc230 = cgraph.addConstraint({ c2, c3, c0 });
    auto cc301 = cgraph.addConstraint({ c3, c0, c1 });

    EXPECT_EQ(4, cgraph.internalComponents().size());
    EXPECT_EQ(4, cgraph.internalConstraints().size());
    
    for (size_t i = 0; i < cgraph.internalComponents().size(); i++){
        CGraph ncgraph = cgraph;
        ncgraph.remove(CGraph::ComponentHandle(i));
        ncgraph.gc();

        EXPECT_EQ(3, ncgraph.internalComponents().size());
        EXPECT_EQ(1, ncgraph.internalConstraints().size());
    }

    for (size_t i = 0; i < cgraph.internalConstraints().size(); i++){
        CGraph ncgraph = cgraph;
        ncgraph.remove(CGraph::ConstraintHandle(i));
        ncgraph.gc();

        EXPECT_EQ(4, ncgraph.internalComponents().size());
        EXPECT_EQ(3, ncgraph.internalConstraints().size());
    }

    cgraph.remove(c0);
    cgraph.remove(c1);
    cgraph.gc();

    EXPECT_EQ(2, cgraph.internalComponents().size());
    EXPECT_EQ(0, cgraph.internalConstraints().size());

}


TEST(GraphicalModelTest, Basic) {

    using CGraph = core::GraphicalModel<core::Dummy, core::LayerConfig<core::Dummy, core::Dynamic>>;

    CGraph cgraph;
    auto c0 = cgraph.add();
    auto c1 = cgraph.add();
    auto c2 = cgraph.add();
    auto c3 = cgraph.add();

    auto cc012 = cgraph.add<1>({ c0, c1, c2 });
    auto cc123 = cgraph.add<1>({ c1, c2, c3 });
    auto cc230 = cgraph.add<1>({ c2, c3, c0 });
    auto cc301 = cgraph.add<1>({ c3, c0, c1 });

    EXPECT_EQ(4, cgraph.internalElements<0>().size());
    EXPECT_EQ(4, cgraph.internalElements<1>().size());

    for (size_t i = 0; i < cgraph.internalElements<0>().size(); i++){
        CGraph ncgraph = cgraph;
        ncgraph.remove(core::HandleAtLevel<0>(i));
        ncgraph.gc();

        EXPECT_EQ(3, ncgraph.internalElements<0>().size());
        EXPECT_EQ(1, ncgraph.internalElements<1>().size());

        int id = 0;
        for (auto c : ncgraph.internalElements<0>()){
            EXPECT_EQ(id++, c.topo.hd.id);
        }
        id = 0;
        for (auto c : ncgraph.internalElements<1>()){
            EXPECT_EQ(id++, c.topo.hd.id);
        }
    }

    for (size_t i = 0; i < cgraph.internalElements<0>().size(); i++){
        CGraph ncgraph = cgraph;
        ncgraph.remove(core::HandleAtLevel<0>(i));
        ncgraph.remove(core::HandleAtLevel<0>((i+1) % ncgraph.internalElements<0>().size()));
        ncgraph.gc();

        EXPECT_EQ(2, ncgraph.internalElements<0>().size());
        EXPECT_EQ(0, ncgraph.internalElements<1>().size());

        int id = 0;
        for (auto c : ncgraph.internalElements<0>()){
            EXPECT_EQ(id++, c.topo.hd.id);
        }
        id = 0;
        for (auto c : ncgraph.internalElements<1>()){
            EXPECT_EQ(id++, c.topo.hd.id);
        }
    }

    for (size_t i = 0; i < cgraph.internalElements<1>().size(); i++){
        CGraph ncgraph = cgraph;
        ncgraph.remove(core::HandleAtLevel<1>(i));
        ncgraph.gc();

        EXPECT_EQ(4, ncgraph.internalElements<0>().size());
        EXPECT_EQ(3, ncgraph.internalElements<1>().size());

        int id = 0;
        for (auto c : ncgraph.internalElements<0>()){
            EXPECT_EQ(id++, c.topo.hd.id);
        }
        id = 0;
        for (auto c : ncgraph.internalElements<1>()){
            EXPECT_EQ(id++, c.topo.hd.id);
        }
    }

    cgraph.remove(c0);
    cgraph.remove(c1);
    cgraph.gc();

    EXPECT_EQ(2, cgraph.internalElements<0>().size());
    EXPECT_EQ(0, cgraph.internalElements<1>().size());

    int id = 0;
    for (auto c : cgraph.internalElements<0>()){
        EXPECT_EQ(id++, c.topo.hd.id);
    }
    id = 0;
    for (auto c : cgraph.internalElements<1>()){
        EXPECT_EQ(id++, c.topo.hd.id);
    }
}



int main(int argc, char * argv[], char * envp[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}