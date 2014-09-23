
#include <QtOpenGL>
#include <QtWidgets>
#include <QWidget>

#include <glut.h>
#include <GL/GLU.h>

#include "../core/macros.hpp"
#include "../core/utilities.hpp"

#include "qt_glue.hpp"
#include "renderable_object_tree.hpp"
#include "singleton.hpp"
#include "visualize3D.hpp"

namespace panoramix {
    namespace vis {

        using namespace core;


        // visualizer parameters
        Visualizer3D::Params::Params()
            :
            winName("Visualizer 3D"),
            backgroundColor(255, 255, 255),
            camera(700, 700, 200, core::Vec3(1, 1, 1) / 4, core::Vec3(0, 0, 0), core::Vec3(0, 0, -1)),
            renderMode(RenderModeFlag::All)
        {}

        Visualizer3D::Status::Status()
            :
            defaultColor(0, 0, 0),
            pointSize(10.0f),
            lineWidth(2.0f),
            colorTable(ColorTableDescriptor::AllColors) 
        {}

        struct Visualizer3D::Visualizer3DPrivateData {
            QList<QWidget *> widgets;
            RenderableObjectTree * renderableObjTree;
            Params params;
            Status status;
            inline Visualizer3DPrivateData(const Params & p, const Status & s) : params(p), status(s) {}
            ~Visualizer3DPrivateData() {
                for (QWidget * w : widgets) {
                    w->deleteLater();
                }
                delete renderableObjTree;
            }
        };
        

        Visualizer3D::Visualizer3D() : _data(std::make_shared<Visualizer3D::Visualizer3DPrivateData>()) {}

        Visualizer3D::Visualizer3D(const Params & p, const Status & s) 
            : _data(std::make_shared<Visualizer3D::Visualizer3DPrivateData>(p, s)) {
        }

        Visualizer3D::~Visualizer3D() {}

        Visualizer3D::Params & Visualizer3D::params() const {
            return _data->params;
        }

        Visualizer3D::Status & Visualizer3D::status() const {
            return _data->status;
        }

        // manipulators
        namespace manip3d {

            Manipulator<const std::string &> SetWindowName(const std::string & name) {
                return Manipulator<const std::string &>(
                    [](Visualizer3D & viz, const std::string & name) {
                    viz.params().winName = name; },
                        name);
            }

            /*Manipulator<Color> SetDefaultColor(Color color) {
                return Manipulator<Color>(
                    [](Visualizer3D & viz, Color c){
                    viz.params().defaultColor = c; },
                        color);
            }

            Manipulator<Color> SetBackgroundColor(Color color) {
                return Manipulator<Color>(
                    [](Visualizer3D & viz, Color c){
                    viz.params().backgroundColor = c; },
                        color);
            }

            Manipulator<const PerspectiveCamera &> SetCamera(const PerspectiveCamera & camera) {
                return Manipulator<const PerspectiveCamera &>(
                    [](Visualizer3D & viz, const PerspectiveCamera & c) {
                    viz.params().camera = c; },
                        camera);
            }

            Manipulator<float> SetPointSize(float pointSize) {
                return Manipulator<float>(
                    [](Visualizer3D & viz, float t){
                    viz.params().pointSize = t; },
                        pointSize);
            }

            Manipulator<float> SetLineWidth(float lineWidth) {
                return Manipulator<float>(
                    [](Visualizer3D & viz, float t){
                    viz.params().lineWidth = t; },
                        lineWidth);
            }

            Manipulator<const vis::ColorTable &> SetColorTable(const vis::ColorTable & colorTable) {
                return Manipulator<const ColorTable &>(
                    [](Visualizer3D & viz, const ColorTable & d) {
                    viz.params().colorTable = d; },
                        colorTable);
            }

            Manipulator<RenderModeFlags> SetRenderMode(RenderModeFlags mode) {
                return Manipulator<RenderModeFlags>(
                    [](Visualizer3D & viz, RenderModeFlags d){
                    viz.params().renderMode = d; },
                        mode);
            }

            Manipulator<const Mat4 &> SetModelMatrix(const Mat4 & mat) {
                return Manipulator<const Mat4 &>(
                    [](Visualizer3D & viz, const Mat4 & m) {
                    viz.params().modelMatrix = m; },
                        mat);
            }*/

           /* void AutoSetCamera(Visualizer3D & viz) {
                auto box = viz.data()->mesh.boundingBox();
                auto center = box.center();
                auto radius = Line3(box.minCorner, box.maxCorner).length() / 2.0;
                viz.params().camera.setCenter(center, false);
                auto eyedirection = viz.params().camera.eye() - viz.params().camera.center();
                eyedirection = eyedirection / core::norm(eyedirection) * radius * 0.8;
                viz.params().camera.setEye(center + eyedirection, false);
                viz.params().camera.setNearAndFarPlanes(radius / 2.0, radius * 2.0, true);
            }*/

            namespace {              


                // visualizer widget
                class Visualizer3DWidget : public QGLWidget {
                public:
                    Visualizer3DWidget(Visualizer3D & viz, QWidget * parent = 0) : QGLWidget(parent), _params(viz.params()){
                        setMouseTracking(true);
                        setAutoBufferSwap(false);
                        _boundingBox = core::Box3();
                        // TODO
                    }


                protected:
                    void initializeGL() {
                        makeCurrent();
                        qglClearColor(MakeQColor(params().backgroundColor));
                        _trianglesObject = new OpenGLObject(this);
                        //_trianglesObject->setUpShaders(OpenGLShaderSourceName::NormalTriangles);
                        _trianglesObject->setUpShaders(PanoramaShader);
                        _trianglesObject->setUpMesh(data()->mesh);
                        if (!data()->texture.empty()){
                            auto im = MakeQImage(data()->texture);
                            auto imcopy = im.copy();
                            _trianglesObject->setUpTexture(imcopy);
                        }
                        _linesObject = new OpenGLObject(this);
                        _linesObject->setUpShaders(OpenGLShaderSourceDescriptor::DefaultLines);
                        _linesObject->setUpMesh(data()->mesh);
                        _pointsObject = new OpenGLObject(this);
                        _pointsObject->setUpShaders(OpenGLShaderSourceDescriptor::DefaultPoints);
                        _pointsObject->setUpMesh(data()->mesh);
                    }

                    void paintGL() {
                        QPainter painter;
                        painter.begin(this);

                        painter.beginNativePainting();
                        qglClearColor(MakeQColor(params().backgroundColor));

                        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                        glFrontFace(GL_CW); // face direction set to clockwise
                        //glCullFace(GL_FRONT); // specify whether front- or back-facing facets can be culled
                        //glEnable(GL_CULL_FACE);
                        glEnable(GL_DEPTH_TEST);
                        glEnable(GL_STENCIL_TEST);

                        glEnable(GL_ALPHA_TEST);

                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


                        glLineWidth(2.0);
                        core::PerspectiveCamera & camera = params().camera;
                        camera.resizeScreen(core::SizeI(width(), height()));
                        QMatrix4x4 modelMatrix = MakeQMatrix(params().modelMatrix);

                        if (params().renderMode & RenderModeFlag::Triangles){
                            _trianglesObject->renderWithCamera(RenderModeFlag::Triangles, camera, modelMatrix);
                        }
                        if (params().renderMode & RenderModeFlag::Points){
                            _pointsObject->renderWithCamera(RenderModeFlag::Points, camera, modelMatrix);
                        }
                        if (params().renderMode & RenderModeFlag::Lines){
                            _linesObject->renderWithCamera(RenderModeFlag::Lines, camera, modelMatrix);
                        }
                        
                        glDisable(GL_DEPTH_TEST);
                        glDisable(GL_CULL_FACE);

                        painter.endNativePainting();
                        swapBuffers();
                    }

                    void resizeGL(int w, int h) {
                        core::PerspectiveCamera & camera = params().camera;
                        camera.resizeScreen(core::Size(w, h));
                        glViewport(0, 0, w, h);
                    }

                private:
                    void moveCameraEyeWithCenterFixed(const QVector3D & t) {
                        core::PerspectiveCamera & camera = params().camera;
                        QVector3D eye = MakeQVec(camera.eye());
                        QVector3D center = MakeQVec(camera.center());
                        QVector3D up = MakeQVec(camera.up());
                        QVector3D tt = t * (eye - center).length() * 0.002f;

                        QVector3D xv = QVector3D::crossProduct(center - eye, up).normalized();
                        QVector3D yv = QVector3D::crossProduct(xv, center - eye).normalized();
                        QVector3D xyTrans = xv * tt.x() + yv * tt.y();
                        double r = ((eye - center).length() - tt.z()) /
                            (eye + xyTrans - center).length();
                        eye = (eye + xyTrans - center) * r + center;
                        up = yv.normalized();
                        params().camera.setEye(MakeCoreVec(eye), false);
                        params().camera.setUp(MakeCoreVec(up), false);
                        
                        auto meshCenter = MakeQVec(_meshBox.center());
                        auto meshRadius = Line3(_meshBox.minCorner, _meshBox.maxCorner).length() / 2.0f;
                        auto nearPlane = (eye - meshCenter).length() - meshRadius;
                        nearPlane = nearPlane < 1e-3 ? 1e-3 : nearPlane;
                        auto farPlane = (eye - meshCenter).length() + meshRadius;
                        params().camera.setNearAndFarPlanes(nearPlane, farPlane, true);
                    }

                    void moveCameraCenterAndCenter(const QVector3D & t) {
                        core::PerspectiveCamera & camera = params().camera;
                        QVector3D eye = MakeQVec(camera.eye());
                        QVector3D center = MakeQVec(camera.center());
                        QVector3D up = MakeQVec(camera.up());
                        QVector3D tt = t * (eye - center).length() * 0.002;

                        QVector3D xv = QVector3D::crossProduct((center - eye), up).normalized();
                        QVector3D yv = QVector3D::crossProduct(xv, (center - eye)).normalized();
                        QVector3D zv = (center - eye).normalized();
                        QVector3D trans = xv * tt.x() + yv * tt.y() + zv * tt.z();
                        eye += trans;
                        center += trans;
                        params().camera.setEye(MakeCoreVec(eye), false);
                        params().camera.setCenter(MakeCoreVec(center), false);
                        
                        auto meshCenter = MakeQVec(_meshBox.center());
                        auto meshRadius = Line3(_meshBox.minCorner, _meshBox.maxCorner).length() / 2.0f;
                        auto nearPlane = (eye - meshCenter).length() - meshRadius;
                        nearPlane = nearPlane < 1e-3 ? 1e-3 : nearPlane;
                        auto farPlane = (eye - meshCenter).length() + meshRadius;
                        params().camera.setNearAndFarPlanes(nearPlane, farPlane, true);
                    }


                protected:
                    virtual void mousePressEvent(QMouseEvent * e) override {
                        _lastPos = e->pos();
                        if (e->buttons() & Qt::RightButton)
                            setCursor(Qt::OpenHandCursor);
                        else if (e->buttons() & Qt::MidButton)
                            setCursor(Qt::SizeAllCursor);
                    }

                    virtual void mouseMoveEvent(QMouseEvent * e) override {
                        QVector3D t(e->pos() - _lastPos);
                        t.setX(-t.x());
                        if (e->buttons() & Qt::RightButton){
                            moveCameraEyeWithCenterFixed(t);
                            setCursor(Qt::ClosedHandCursor);
                            update();
                        }
                        else if (e->buttons() & Qt::MidButton){
                            moveCameraCenterAndCenter(t);
                            update();
                        }
                        _lastPos = e->pos();
                    }

                    virtual void wheelEvent(QWheelEvent * e) override {
                        moveCameraCenterAndCenter(QVector3D(0, 0, e->delta() / 10));
                        update();
                    }

                    virtual void mouseReleaseEvent(QMouseEvent * e) override {
                        unsetCursor();
                    }                    

                private:
                    Visualizer3D::Params _params;
                    QPointF _lastPos;
                    OpenGLObject * _linesObject;
                    OpenGLObject * _pointsObject;
                    OpenGLObject * _trianglesObject;
                    core::Box3 _boundingBox;
                };
                

            }


            Manipulator<bool> Show(bool doModel) {
                return Manipulator<bool>(
                    [](Visualizer3D & viz, bool modal){                  
                    auto app = Singleton::InitGui();
                    Visualizer3DWidget * w = new Visualizer3DWidget(viz);
                    viz.widgets()->ws.append(w);
                    w->resize(MakeQSize(viz.data()->params.camera.screenSize()));
                    w->setWindowTitle(QString::fromStdString(viz.data()->params.winName));
                    w->show();
                    if (modal){                        
                        Singleton::ContinueGui(); // qApp->exec()
                    }               
                },
                    doModel);
            }

        }


        Visualizer3D operator << (Visualizer3D viz, const core::Point3 & p) {
            OpenGLMesh::Vertex v;
            v.position4 = VectorFromHPoint(core::HPoint3(p, 1.0));
            v.color4 = viz.params().defaultColor / 255.0f;
            //v.lineWidth1 = viz.params().lineWidth;
            //v.pointSize1 = viz.params().pointSize;
            viz.data()->mesh.addVertex(v);
            return viz;
        }


        Visualizer3D operator << (Visualizer3D viz, const core::Line3 & p) {
            auto & mesh = viz.data()->mesh;
            core::Point3 ps[] = { p.first, p.second };
            OpenGLMesh::Vertex vs[2];
            for (int i = 0; i < 2; i++){
                vs[i].position4 = (VectorFromHPoint(core::HPoint3(ps[i], 1.0)));
                vs[i].color4 = (viz.params().defaultColor) / 255.0f;
                //vs[i].lineWidth1 = viz.params().lineWidth;
                //vs[i].pointSize1 = viz.params().pointSize;
            }
            viz.data()->mesh.addIsolatedLine(vs[0], vs[1]);
            return viz;
        }


        Visualizer3D operator << (Visualizer3D viz, const Image & tex) {
            viz.data()->texture = tex;
            return viz;
        }

        Visualizer3D operator << (Visualizer3D viz, const std::vector<std::pair<Point3, Point2>> & polygonWithTexCoords) {
            std::vector<OpenGLMesh::VertHandle> vhs;
            vhs.reserve(polygonWithTexCoords.size());
            if (polygonWithTexCoords.size() <= 2)
                return viz; // TODO
            auto normal = (polygonWithTexCoords[0].first - polygonWithTexCoords[1].first)
                .cross(polygonWithTexCoords[2].first - polygonWithTexCoords[1].first);
            normal /= norm(normal);
            for (auto & p : polygonWithTexCoords){
                OpenGLMesh::Vertex v;
                v.position4 = (VectorFromHPoint(core::HPoint3(p.first, 1.0)));
                v.color4 = (viz.params().defaultColor) / 255.0f;
                //v.lineWidth1 = viz.params().lineWidth;
                //v.pointSize1 = viz.params().pointSize;
                v.texCoord2 = (p.second);
                v.normal3 = (normal);
                vhs.push_back(viz.data()->mesh.addVertex(v));
            }
            viz.data()->mesh.addPolygon(vhs);
            return viz;
        }











        // visualizer parameters
        AdvancedVisualizer3D::Params::Params()
            :
            winName("Advanced Visualizer 3D"),
            backgroundColor(10, 10, 10),
            camera(700, 700, 200, core::Vec3(1, 1, 1) / 4, core::Vec3(0, 0, 0), core::Vec3(0, 0, -1)),
            colorTableDescriptor(ColorTableDescriptor::AllColors),
            modelMatrix(core::Mat4::eye())
        {}

        struct AdvancedVisualizer3D::VisualData {
            OpenGLMesh mesh;
            Image texture;
            AdvancedVisualizer3D::Params params;
        };

        struct AdvancedVisualizer3D::Widgets {
            QList<QWidget *> ws;
        };

    }
}