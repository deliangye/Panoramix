#include <filesystem>

extern "C" {
    #include <gpc.h>
}

#include <glpk.h>
#include <setjmp.h>

#include <Eigen/StdVector>
#include <dlib/matrix.h>
#include <dlib/optimization.h>

#include <GCoptimization.h>

#include "../vis/visualize2d.hpp"
#include "../vis/visualize3d.hpp"

#include "optimization.hpp"
#include "reconstruction_engine.hpp"

#include "../core/debug.hpp"

namespace panoramix {
    namespace rec {

        // for glpk
        struct sinfo {
            char * text;
            jmp_buf * env;
        };

        void glpErrorHook(void * in){
            sinfo * info = (sinfo*)in;
            glp_free_env();
            longjmp(*(info->env), 1);
        }


        ReconstructionEngine::Params::Params() 
            : camera(250.0), cameraAngleScaler(1.8), smallCameraAngleScalar(0.05),
            samplingStepLengthOnRegionBoundaries(16.0),
            samplingStepLengthOnLines(8.0),
            intersectionDistanceThreshold(30),
            incidenceDistanceAlongDirectionThreshold(50),
            incidenceDistanceVerticalDirectionThreshold(8)
        {}


        ReconstructionEngine::ViewHandle ReconstructionEngine::insertPhoto(const Image & im, const PerspectiveCamera & cam,
            double cameraDirectionErrorScale) {
            ViewData vd;
            vd.camera = vd.originalCamera = cam;
            vd.cameraDirectionErrorScale = cameraDirectionErrorScale;
            vd.image = im;
            return insertView(vd);
        }

        void ReconstructionEngine::insertPanorama(const Image & panorama, const std::vector<PerspectiveCamera> & viewCams,
            const PanoramicCamera & panCam) {
            for (int i = 0; i < viewCams.size(); i++) {
                auto & camera = viewCams[i];
                const auto im =
                    core::CameraSampler<core::PerspectiveCamera, core::PanoramicCamera>(camera, panCam)(panorama);
                auto viewHandle = insertPhoto(im, camera);
                updateConnections(viewHandle);
            }
            _globalData.panorama = panorama;
        }

        namespace {

            void LineIntersectons(const std::vector<Classified<Line2>> & lines,
                std::vector<HPoint2> & hinterps, std::vector<std::pair<int, int>> & lineids,
                bool suppresscross)
            {
                size_t lnum = lines.size();
                for (int i = 0; i < lnum; i++){
                    auto eqi = cv::Vec3d(lines[i].component.first[0], lines[i].component.first[1], 1)
                        .cross(cv::Vec3d(lines[i].component.second[0], lines[i].component.second[1], 1));
                    for (int j = i + 1; j < lnum; j++){
                        auto eqj = cv::Vec3d(lines[j].component.first[0], lines[j].component.first[1], 1)
                            .cross(cv::Vec3d(lines[j].component.second[0], lines[j].component.second[1], 1));
                        auto interp = eqi.cross(eqj);
                        if (interp[0] == 0 && interp[1] == 0 && interp[2] == 0){ // lines overlapped
                            interp[0] = -eqi[1];
                            interp[1] = eqi[0];
                        }
                        interp /= norm(interp);

                        if (suppresscross){
                            auto& a1 = lines[i].component.first;
                            auto& a2 = lines[i].component.second;
                            auto& b1 = lines[j].component.first;
                            auto& b2 = lines[j].component.second;
                            double q = a1[0] * b1[1] - a1[1] * b1[0] - a1[0] * b2[1] + a1[1] * b2[0] -
                                a2[0] * b1[1] + a2[1] * b1[0] + a2[0] * b2[1] - a2[1] * b2[0];
                            double t = (a1[0] * b1[1] - a1[1] * b1[0] - a1[0] * b2[1] +
                                a1[1] * b2[0] + b1[0] * b2[1] - b1[1] * b2[0]) / q;
                            if (t > 0 && t < 1 && t == t)
                                continue;
                        }
                        hinterps.push_back(HPointFromVector(interp));
                        lineids.push_back({ i, j });
                    }
                }
            }
        }

        void ReconstructionEngine::computeFeatures(ViewHandle h) {
            auto & vd = _views.data(h);
            const Image & im = vd.image;

            // regions
            RegionsNet::Params regionsNetParams;
            regionsNetParams.samplingStepLengthOnBoundary = _params.samplingStepLengthOnRegionBoundaries;
            vd.regionNet = std::make_shared<RegionsNet>(vd.image, regionsNetParams);
            vd.regionNet->buildNetAndComputeGeometricFeatures();
            vd.regionNet->computeImageFeatures();

            // lines
            LinesNet::Params linesNetParams;
            linesNetParams.intersectionDistanceThreshold = _params.intersectionDistanceThreshold;
            linesNetParams.incidenceDistanceVerticalDirectionThreshold = _params.incidenceDistanceVerticalDirectionThreshold;
            linesNetParams.incidenceDistanceAlongDirectionThreshold = _params.incidenceDistanceAlongDirectionThreshold;
            vd.lineNet = std::make_shared<LinesNet>(vd.image, linesNetParams);
        }






        namespace {
            inline double PerspectiveCameraAngleRadius(const PerspectiveCamera & cam) {
                return atan(sqrt(Square(cam.screenSize().height) + Square(cam.screenSize().width)) /
                    2.0 / cam.focal());
            }
        }

        size_t ReconstructionEngine::updateConnections(ViewHandle h) {
            auto & thisv = _views.data(h);
            const PerspectiveCamera & thisvCam = thisv.originalCamera;
            double thisvCamAngleRadius = PerspectiveCameraAngleRadius(thisvCam);
            thisvCamAngleRadius *= _params.cameraAngleScaler;
            for (auto & v : _views.elements<0>()){
                if (v.topo.hd == h)
                    continue;
                const PerspectiveCamera & vcam = v.data.originalCamera;
                double vCamAngleRadius = PerspectiveCameraAngleRadius(vcam);
                vCamAngleRadius *= _params.cameraAngleScaler;
                double angleDistance = AngleBetweenDirections(thisvCam.center(), vcam.center());
                if (angleDistance <= thisvCamAngleRadius + vCamAngleRadius){
                    // may overlap
                    ViewConnectionData hd;
                    //hd.cameraAngleDistance = angleDistance;
                    _views.add<1>({ h, v.topo.hd }, hd);
                }
            }
            return _views.topo(h).uppers.size();
        }

        ReconstructionEngine::ViewHandle ReconstructionEngine::isTooCloseToAnyExistingView(ViewHandle h) const {
            auto & camera = _views.data(h).camera;
            double cameraRadius = PerspectiveCameraAngleRadius(camera);
            auto connections = _views.topo(h).uppers;
            for (auto & con : connections){
                auto to = _views.topo(con).lowers[0];
                if (to == h)
                    to = _views.topo(con).lowers[1];
                auto & neighborCamera = _views.data(to).camera;
                double cameraAngle = AngleBetweenDirections(camera.center(), neighborCamera.center());
                double neighborCameraRadius = PerspectiveCameraAngleRadius(camera);
                if (cameraAngle <= (cameraRadius + neighborCameraRadius) * _params.smallCameraAngleScalar){ // too close
                    return to;
                }
            }
            return ViewHandle();
        }












        namespace {

            inline PixelLoc PixelIndexFromGeoCoord(const GeoCoord & p, int longidiv, int latidiv) {
                int longtid = static_cast<int>((p.longitude + M_PI) * longidiv / M_PI / 2);
                int latid = static_cast<int>((p.latitude + M_PI_2) * latidiv / M_PI);
                longtid = (longtid % longidiv + longidiv) % longidiv;
                latid = (latid % latidiv + latidiv) % latidiv;
                return PixelLoc(longtid, latid);
            }

            inline GeoCoord GeoCoordFromPixelIndex(const cv::Point & pixel, int longidiv, int latidiv) {
                return GeoCoord{ pixel .x * M_PI * 2 / longidiv - M_PI, pixel.y * M_PI / latidiv - M_PI_2 };
            }

            inline double LatitudeFromLongitudeAndNormalVector(double longitude, const Vec3 & normal) {
                // normal(0)*cos(long)*cos(la) + normal(1)*sin(long)*cos(lat) + normal(2)*sin(la) = 0
                // normal(0)*cos(long) + normal(1)*sin(long) + normal(2)*tan(la) = 0
                return -atan((normal(0)*cos(longitude) + normal(1)*sin(longitude)) / normal(2));
            }

            inline double Longitude1FromLatitudeAndNormalVector(double latitude, const Vec3 & normal) {
                double a = normal(1) * cos(latitude);
                double b = normal(0) * cos(latitude);
                double c = -normal(2) * sin(latitude);
                double sinLong = (a * c + sqrt(Square(a*c) - (Square(a) + Square(b))*(Square(c) - Square(b)))) / (Square(a) + Square(b));
                return asin(sinLong);
            }

            inline double Longitude2FromLatitudeAndNormalVector(double latitude, const Vec3 & normal) {
                double a = normal(1) * cos(latitude);
                double b = normal(0) * cos(latitude);
                double c = -normal(2) * sin(latitude);
                double sinLong = (a * c - sqrt(Square(a*c) - (Square(a) + Square(b))*(Square(c) - Square(b)))) / (Square(a) + Square(b));
                return asin(sinLong);
            }

            inline double UnOrthogonality(const Vec3 & v1, const Vec3 & v2, const Vec3 & v3) {
                return norm(Vec3(v1.dot(v2), v2.dot(v3), v3.dot(v1)));
            }

            std::array<Vec3, 3> FindVanishingPoints(const std::vector<Vec3>& intersections,
                int longitudeDivideNum = 1000, int latitudeDivideNum = 500) {

                std::array<Vec3, 3> vps;

                cv::Mat votePanel = cv::Mat::zeros(longitudeDivideNum, latitudeDivideNum, CV_32FC1);

                std::cout << "begin voting ..." << std::endl;
                size_t pn = intersections.size();
                for (const Vec3& p : intersections){
                    PixelLoc pixel = PixelIndexFromGeoCoord(GeoCoord(p), longitudeDivideNum, latitudeDivideNum);
                    votePanel.at<float>(pixel.x, pixel.y) += 1.0;
                }
                std::cout << "begin gaussian bluring ..." << std::endl;
                cv::GaussianBlur(votePanel, votePanel, cv::Size((longitudeDivideNum / 50) * 2 + 1, (latitudeDivideNum / 50) * 2 + 1),
                    4, 4, cv::BORDER_REPLICATE);
                std::cout << "done voting" << std::endl;

                double minVal = 0, maxVal = 0;
                int maxIndex[] = { -1, -1 };
                cv::minMaxIdx(votePanel, & minVal, & maxVal, 0, maxIndex);
                cv::Point maxPixel(maxIndex[0], maxIndex[1]);

                vps[0] = GeoCoordFromPixelIndex(maxPixel, longitudeDivideNum, latitudeDivideNum).toVector();
                const Vec3 & vec0 = vps[0];

                // iterate locations orthogonal to vps[0]
                double maxScore = -1;
                for (int x = 0; x < longitudeDivideNum; x++){
                    double longt1 = double(x) / longitudeDivideNum * M_PI * 2 - M_PI;
                    double lat1 = LatitudeFromLongitudeAndNormalVector(longt1, vec0);
                    Vec3 vec1 = GeoCoord(longt1, lat1).toVector();
                    Vec3 vec1rev = -vec1;
                    Vec3 vec2 = vec0.cross(vec1);
                    Vec3 vec2rev = -vec2;
                    Vec3 vecs[] = { vec1, vec1rev, vec2, vec2rev };

                    double score = 0;
                    for (Vec3 & v : vecs){
                        PixelLoc pixel = PixelIndexFromGeoCoord(GeoCoord(v), longitudeDivideNum, latitudeDivideNum);
                        score += votePanel.at<float>(WrapBetween(pixel.x, 0, longitudeDivideNum), 
                            WrapBetween(pixel.y, 0, latitudeDivideNum));
                    }
                    if (score > maxScore){
                        maxScore = score;
                        vps[1] = vec1;
                        vps[2] = vec2;
                    }
                }

                if (UnOrthogonality(vps[0], vps[1], vps[2]) < 0.1)
                    return vps;
                
                // failed, then use y instead of x
                maxScore = -1;
                for (int y = 0; y < latitudeDivideNum; y++){
                    double lat1 = double(y) / latitudeDivideNum * M_PI - M_PI_2;
                    double longt1s[] = { Longitude1FromLatitudeAndNormalVector(lat1, vec0), 
                        Longitude2FromLatitudeAndNormalVector(lat1, vec0) };
                    for (double longt1 : longt1s){
                        Vec3 vec1 = GeoCoord(longt1, lat1).toVector();
                        Vec3 vec1rev = -vec1;
                        Vec3 vec2 = vec0.cross(vec1);
                        Vec3 vec2rev = -vec2;
                        Vec3 vecs[] = { vec1, vec1rev, vec2, vec2rev };

                        double score = 0;
                        for (Vec3 & v : vecs){
                            PixelLoc pixel = PixelIndexFromGeoCoord(GeoCoord(v), longitudeDivideNum, latitudeDivideNum);
                            score += votePanel.at<float>(WrapBetween(pixel.x, 0, longitudeDivideNum), 
                                WrapBetween(pixel.y, 0, latitudeDivideNum));
                        }
                        if (score > maxScore){
                            maxScore = score;
                            vps[1] = vec1;
                            vps[2] = vec2;
                        }
                    }
                }

                assert(UnOrthogonality(vps[0], vps[1], vps[2]) < 0.1);

                return vps;

            }

            template <class Vec3Container>
            void ClassifyLines(const Vec3Container & points, std::vector<Classified<Line3>> & lines,
                double angleThreshold = M_PI/3, double sigma = 0.1) {
                
                size_t nlines = lines.size();
                size_t npoints = points.size();

                for (size_t i = 0; i < nlines; i++){
                    Vec3 a = lines[i].component.first;
                    Vec3 b = lines[i].component.second;
                    Vec3 normab = a.cross(b);
                    normab /= norm(normab);

                    std::vector<double> lineangles(npoints);
                    std::vector<double> linescores(npoints);

                    for (int j = 0; j < npoints; j++){
                        Vec3 point = points[j];
                        double angle = abs(asin(normab.dot(point)));
                        lineangles[j] = angle;
                    }

                    // get score based on angle
                    for (int j = 0; j < npoints; j++){
                        double angle = lineangles[j];
                        double score = exp(-(angle / angleThreshold) * (angle / angleThreshold) / sigma / sigma / 2);
                        linescores[j] = (angle > angleThreshold) ? 0 : score;
                    }

                    // classify lines
                    lines[i].claz = -1;
                    double curscore = 0.8;
                    for (int j = 0; j < npoints; j++){
                        if (linescores[j] > curscore){
                            lines[i].claz = j;
                            curscore = linescores[j];
                        }
                    }
                }
            }

            inline Vec3 RotateDirectionTo(const Vec3 & originalDirection, const Vec3 & toDirection, double angle) {
                Vec3 tovec = originalDirection.cross(toDirection).cross(originalDirection);
                Vec3 result3 = originalDirection + tovec * tan(angle);
                return result3 / norm(result3);
            }
            
        }


        void ReconstructionEngine::estimateVanishingPointsAndClassifyLines() {

            // pick separated views only
            std::vector<decltype(_views.elements<0>().begin())> seperatedViewIters;
            MergeNearNaive(_views.elements<0>().begin(), _views.elements<0>().end(), std::back_inserter(seperatedViewIters), std::false_type(),
                _params.smallCameraAngleScalar,
                [](const ViewsGraph::TripletType<0> & v1, const ViewsGraph::TripletType<0> & v2) -> double{
                double angleDistance = AngleBetweenDirections(v1.data.camera.center(), v2.data.camera.center());
                return angleDistance / 
                    (PerspectiveCameraAngleRadius(v1.data.camera) + PerspectiveCameraAngleRadius(v2.data.camera));
            });

            // collect line intersections
            size_t lineIntersectionsNum = 0;
            for (const auto & vIter : seperatedViewIters) // count intersecton num
                lineIntersectionsNum += vIter->data.lineNet->lineSegmentIntersections().size();
            std::vector<Vec3> intersections(lineIntersectionsNum);
            auto intersectionsBegin = intersections.begin();
            for (const auto & vIter : seperatedViewIters){ // projection 2d intersections to global GeoCoord
                intersectionsBegin = std::transform(vIter->data.lineNet->lineSegmentIntersections().begin(), 
                    vIter->data.lineNet->lineSegmentIntersections().end(),
                    intersectionsBegin, 
                    [&vIter](const HPoint2 & p) -> Vec3 {
                    Vec3 p3 = vIter->data.camera.spatialDirection(p.toPoint());
                    return p3 / norm(p3); // normalized
                });
            }

            // find vanishing points;
            _globalData.vanishingPoints = FindVanishingPoints(intersections);           


            // add spatial line segments from line segments of all views
            size_t spatialLineSegmentsNum = 0;
            for (auto & v : _views.elements<0>())
                spatialLineSegmentsNum += v.data.lineNet->lineSegments().size();
            std::vector<Classified<Line3>> spatialLineSegments(spatialLineSegmentsNum);
            auto spatialLineSegmentBegin = spatialLineSegments.begin();
            for (auto & v : _views.elements<0>()){
                spatialLineSegmentBegin = std::transform(v.data.lineNet->lineSegments().begin(),
                    v.data.lineNet->lineSegments().end(),
                    spatialLineSegmentBegin, [&v](const Line2 & line) -> Classified<Line3>{
                    auto & p1 = line.first;
                    auto & p2 = line.second;
                    auto pp1 = v.data.camera.spatialDirection(p1);
                    auto pp2 = v.data.camera.spatialDirection(p2);
                    Classified<Line3> cline3;
                    cline3.claz = -1;
                    cline3.component = Line3{ pp1, pp2 };
                    return cline3;
                });
            }

            // classify lines
            ClassifyLines(_globalData.vanishingPoints, spatialLineSegments);

            // build lines net and compute features
            spatialLineSegmentBegin = spatialLineSegments.begin();
            for (auto & v : _views.elements<0>()){
                std::array<HPoint2, 3> projectedVPs;
                for (int i = 0; i < 3; i++){
                    projectedVPs[i] = v.data.camera.screenProjectionInHPoint(_globalData.vanishingPoints[i]);
                }
                std::vector<int> lineClasses(v.data.lineNet->lineSegments().size());
                for (auto & lineClass : lineClasses){
                    lineClass = spatialLineSegmentBegin->claz;
                    ++spatialLineSegmentBegin;
                }
                v.data.lineNet->buildNetAndComputeFeaturesUsingVanishingPoints(projectedVPs, lineClasses);
            }


        }

        namespace {

            // polygon conversion
            void ConvertToGPCPolygon(const std::vector<PixelLoc> & pts, gpc_polygon & poly) {
                poly.num_contours = 1;
                poly.contour = new gpc_vertex_list[1];
                poly.contour[0].num_vertices = pts.size();
                poly.contour[0].vertex = new gpc_vertex[pts.size()];
                for (int i = 0; i < pts.size(); i++) {
                    poly.contour[0].vertex[i].x = pts[i].x;
                    poly.contour[0].vertex[i].y = pts[i].y;
                }
                poly.hole = new int[1];
                poly.hole[0] = 0;
            }

            void ConvertToPixelVector(const gpc_polygon & poly, std::vector<PixelLoc> & pts) {
                pts.clear();
                pts.resize(poly.contour[0].num_vertices);
                for (int i = 0; i < pts.size(); i++) {
                    pts[i].x = static_cast<int>(poly.contour[0].vertex[i].x);
                    pts[i].y = static_cast<int>(poly.contour[0].vertex[i].y);
                }
            }

            // line depth ratio
            double ComputeDepthRatioOfPointOnSpatialLine(Vec3 lineFirstPointDir,
                Vec3 p, Vec3 vp) {
                // firstp -> p vp
                //  \      /
                //   \    /
                //    center
                lineFirstPointDir /= norm(lineFirstPointDir);
                p /= norm(p);
                vp /= norm(vp);

                if ((p - lineFirstPointDir).dot(vp) < 0)
                    vp = -vp;
                double angleCenter = AngleBetweenDirections(lineFirstPointDir, p);
                double angleFirstP = AngleBetweenDirections(-lineFirstPointDir, vp);
                double angleP = AngleBetweenDirections(-p, -vp);
                //assert(FuzzyEquals(angleCenter + angleFirstP + angleP, M_PI, 0.1));
                return sin(angleFirstP) / sin(angleP);
            }


        }


        void ReconstructionEngine::recognizeRegionLineRelations() {

            //// REGIONS ////

            // compute spatial positions of each region
            IndexHashMap<RegionIndex, std::vector<Vec3>>
                regionSpatialContours;
            for (auto & view : _views.elements<0>()) {
                const auto & regions = *view.data.regionNet;
                for (auto & region : regions.regions().elements<0>()) {
                    RegionIndex ri = { view.topo.hd, region.topo.hd };
                    const ReconstructionEngine::ViewData & vd = view.data;
                    const RegionsNet::RegionData & rd = region.data;

                    assert(!rd.contours.empty() && "Region contour not initialized yet?");
                    std::vector<Vec3> spatialContour;
                    for (auto & p : rd.dilatedContours.back()) {
                        auto direction = vd.camera.spatialDirection(p);
                        spatialContour.push_back(direction / norm(direction));
                    }
                    regionSpatialContours[ri] = spatialContour;
                }
            }

            // build spatial rtree for regions
            auto lookupRegionBB = [&regionSpatialContours](const RegionIndex& ri) {
                return BoundingBoxOfContainer(regionSpatialContours[ri]);
            };

            RTreeWrapper<RegionIndex, decltype(lookupRegionBB)> regionsRTree(lookupRegionBB);
            for (auto & region : regionSpatialContours) {
                regionsRTree.insert(region.first);
            }

            // store overlapping ratios between overlapped regions
            IndexHashMap<std::pair<RegionIndex, RegionIndex>, double> &
                overlappedRegionIndexPairs = _globalData.overlappedRegionIndexPairs;
            overlappedRegionIndexPairs.clear();

            for (auto & rip : regionSpatialContours) {
                auto & ri = rip.first;
                auto & riContour2d = regionData(ri).contours.front();
                auto & riCamera = _views.data(ri.viewHandle).camera;
                double riArea = regionData(ri).area;

                gpc_polygon riPoly;
                ConvertToGPCPolygon(riContour2d, riPoly);

                regionsRTree.search(lookupRegionBB(ri),
                    [&ri, &riContour2d, &riPoly, &riCamera, riArea, &overlappedRegionIndexPairs, &regionSpatialContours](
                    const RegionIndex & relatedRi) {

                    if (ri.viewHandle == relatedRi.viewHandle) {
                        return true;
                    }

                    // project relatedRi contour to ri's camera plane
                    auto & relatedRiContour3d = regionSpatialContours[relatedRi];
                    std::vector<core::PixelLoc> relatedRiContour2d(relatedRiContour3d.size());
                    for (int i = 0; i < relatedRiContour3d.size(); i++) {
                        auto p = riCamera.screenProjection(relatedRiContour3d[i]);
                        relatedRiContour2d[i] = PixelLoc(p);
                    }
                    gpc_polygon relatedRiPoly;
                    ConvertToGPCPolygon(relatedRiContour2d, relatedRiPoly);

                    // compute overlapping area ratio
                    gpc_polygon intersectedPoly;
                    gpc_polygon_clip(GPC_INT, &relatedRiPoly, &riPoly, &intersectedPoly);

                    if (intersectedPoly.num_contours > 0 && intersectedPoly.contour[0].num_vertices > 0) {
                        std::vector<core::PixelLoc> intersected;
                        ConvertToPixelVector(intersectedPoly, intersected);
                        double intersectedArea = cv::contourArea(intersected);

                        double overlapRatio = intersectedArea / riArea;
                        assert(overlapRatio <= 1.0 && "Invalid overlap ratio!");

                        if (overlapRatio > 0.2)
                            overlappedRegionIndexPairs[std::make_pair(relatedRi, ri)] = overlapRatio;
                    }

                    gpc_free_polygon(&relatedRiPoly);
                    gpc_free_polygon(&intersectedPoly);

                    return true;
                });

                gpc_free_polygon(&riPoly);
            }

            for (auto & riPair : overlappedRegionIndexPairs) {
                auto revRiPair = std::make_pair(riPair.first.second, riPair.first.first);
                std::cout << "a-b: " << riPair.second;
                if (overlappedRegionIndexPairs.find(revRiPair) != overlappedRegionIndexPairs.end())
                    std::cout << "   b-a: " << overlappedRegionIndexPairs[revRiPair];
                std::cout << std::endl;
            }



            //// LINES ////
            // compute spatial normal directions for each line
            IndexHashMap<LineIndex, Classified<Line3>>
                lineSpatialAvatars;
            for (auto & vd : _views.elements<0>()) {
                auto & lines = vd.data.lineNet->lines();
                LineIndex li;
                li.viewHandle = vd.topo.hd;
                auto & cam = vd.data.camera;
                for (auto & ld : lines.elements<0>()) {
                    li.handle = ld.topo.hd;
                    auto & line = ld.data.line;
                    Classified<Line3> avatar;
                    avatar.claz = line.claz;
                    avatar.component = Line3(
                        cam.spatialDirection(line.component.first),
                        cam.spatialDirection(line.component.second)
                        );
                    lineSpatialAvatars[li] = avatar;
                }
            }

            // build rtree for lines
            auto lookupLineNormal = [&lineSpatialAvatars](const LineIndex & li) -> Box3 {
                auto normal = lineSpatialAvatars[li].component.first.cross(lineSpatialAvatars[li].component.second);
                Box3 b = BoundingBox(normalize(normal));
                static const double s = 0.2;
                b.minCorner = b.minCorner - Vec3(s, s, s);
                b.maxCorner = b.maxCorner + Vec3(s, s, s);
                return b;
            };

            RTreeWrapper<LineIndex, decltype(lookupLineNormal)> linesRTree(lookupLineNormal);
            for (auto & i : lineSpatialAvatars) {
                linesRTree.insert(i.first);
            }

            // recognize incidence constraints between lines of different views
            IndexHashMap<std::pair<LineIndex, LineIndex>, Vec3> & lineIncidenceRelations 
                = _globalData.lineIncidenceRelationsAcrossViews;
            lineIncidenceRelations.clear();

            for (auto & i : lineSpatialAvatars) {
                auto li = i.first;
                auto & lineData = i.second;
                auto & views = _views;
                linesRTree.search(lookupLineNormal(li), [&li, &lineSpatialAvatars, &views, &lineIncidenceRelations](const LineIndex & relatedLi) -> bool {
                    if (li.viewHandle == relatedLi.viewHandle)
                        return true;
                    if (relatedLi < li) // make sure one relation is stored only once, avoid storing both a-b and b-a
                        return true;
                    auto & line1 = lineSpatialAvatars[li];
                    auto & line2 = lineSpatialAvatars[relatedLi];
                    if (line1.claz != line2.claz)
                        return true;
                    auto normal1 = normalize(line1.component.first.cross(line1.component.second));
                    auto normal2 = normalize(line2.component.first.cross(line2.component.second));
                    auto & vd1 = views.data(li.viewHandle);
                    auto & vd2 = views.data(relatedLi.viewHandle);
                    if (std::min(AngleBetweenDirections(normal1, normal2),
                        AngleBetweenDirections(normal1, -normal2)) <
                        vd1.lineNet->params().incidenceDistanceVerticalDirectionThreshold / vd1.camera.focal() +
                        vd2.lineNet->params().incidenceDistanceVerticalDirectionThreshold / vd2.camera.focal()) {

                        auto nearest = DistanceBetweenTwoLines(line1.component, line2.component);
                        auto relationCenter = (nearest.second.first.position + nearest.second.second.position) / 2.0;
                        relationCenter /= norm(relationCenter);

                        lineIncidenceRelations[std::make_pair(li, relatedLi)] = relationCenter;
                    }
                    return true;
                });
            }


            // generate sampled points for line-region connections
            IndexHashMap<std::pair<RegionIndex, LineIndex>, std::vector<Vec3>> &
                regionLineIntersectionSampledPoints = _globalData.regionLineIntersectionSampledPoints;
            regionLineIntersectionSampledPoints.clear();

            static const int extendSize = 5;
            std::vector<int> dx, dy;
            dx.reserve(2 * extendSize + 1);
            dy.reserve(2 * extendSize + 1);
            for (int a = -extendSize; a <= extendSize; a++) {
                for (int b = -extendSize; b <= extendSize; b++) {
                    dx.push_back(a);
                    dy.push_back(b);
                }
            }

            for (auto & vd : _views.elements<0>()) {
                RegionIndex ri;
                ri.viewHandle = vd.topo.hd;

                LineIndex li;
                li.viewHandle = vd.topo.hd;

                const Image & segmentedRegions = vd.data.regionNet->segmentedRegions();
                auto & cam = vd.data.camera;

                for (auto & ld : vd.data.lineNet->lines().elements<0>()) {
                    li.handle = ld.topo.hd;

                    auto & line = ld.data.line.component;
                    auto lineDir = normalize(line.direction());
                    double sampleStep = _params.samplingStepLengthOnLines;
                    int sampledNum = static_cast<int>(std::floor(line.length() / sampleStep));

                    for (int i = 0; i < sampledNum; i++) {
                        auto sampledPoint = line.first + lineDir * i * sampleStep;

                        std::set<int32_t> rhids;
                        for (int k = 0; k < dx.size(); k++) {
                            int x = BoundBetween(static_cast<int>(std::round(sampledPoint[0] + dx[k])), 0, segmentedRegions.cols - 1);
                            int y = BoundBetween(static_cast<int>(std::round(sampledPoint[1] + dy[k])), 0, segmentedRegions.rows - 1);
                            PixelLoc p(x, y);
                            rhids.insert(segmentedRegions.at<int32_t>(p));
                        }

                        for (int32_t rhid : rhids) {
                            ri.handle = RegionsNet::RegionHandle(rhid);
                            regionLineIntersectionSampledPoints[std::make_pair(ri, li)]
                                .push_back(normalize(cam.spatialDirection(sampledPoint)));
                        }
                    }
                }
            }


            IF_DEBUG_USING_VISUALIZERS{
                // visualize connections between regions and lines
                std::unordered_map<ViewHandle, vis::Visualizer2D, HandleHasher<AtLevel<0>>> vizs;
                for (auto & vd : _views.elements<0>()) {
                    //vis::Visualizer2D viz(vd.data.regionNet->image);
                    int height = vd.data.regionNet->image().rows;
                    int width = vd.data.regionNet->image().cols;

                    Image coloredOutput(vd.data.regionNet->segmentedRegions().size(), CV_8UC3);
                    std::vector<cv::Vec<uint8_t, 3>> colors(vd.data.regionNet->regions().internalElements<0>().size());
                    std::generate(colors.begin(), colors.end(), []() {
                        return cv::Vec<uint8_t, 3>(uint8_t(std::rand() % 256),
                            uint8_t(std::rand() % 256),
                            uint8_t(std::rand() % 256));
                    });
                    for (int y = 0; y < height; y++) {
                        for (int x = 0; x < width; x++) {
                            coloredOutput.at<cv::Vec<uint8_t, 3>>(cv::Point(x, y)) =
                                colors[vd.data.regionNet->segmentedRegions().at<int32_t>(cv::Point(x, y))];
                        }
                    }
                    vizs[vd.topo.hd].setImage(vd.data.regionNet->image());
                    vizs[vd.topo.hd].params.alphaForNewImage = 0.5;
                    vizs[vd.topo.hd] << coloredOutput;
                }

                for (auto & lineIdRi : regionLineIntersectionSampledPoints) {
                    auto & ri = lineIdRi.first.first;
                    auto & li = lineIdRi.first.second;
                    auto & cline2 = _views.data(li.viewHandle).lineNet->lines().data(li.handle).line;
                    auto & cam = _views.data(ri.viewHandle).camera;
                    auto & viz = vizs[ri.viewHandle];

                    viz << vis::manip2d::SetColor(vis::ColorTag::Black)
                        << vis::manip2d::SetThickness(1);
                    auto & regionCenter = _views.data(ri.viewHandle).regionNet->regions().data(ri.handle).center;
                    for (auto & d : lineIdRi.second) {
                        auto p = cam.screenProjection(d);
                        viz << Line2(regionCenter, p);
                    }
                }

                for (auto & viz : vizs) {
                    viz.second << vis::manip2d::Show();
                }
            }
        }


        void ReconstructionEngine::estimateSpatialLineDepths() {

            NOT_IMPLEMENTED_YET();

        }

        namespace {

            template <class FunctorT>
            struct DataCostFunctorWrapper : GCoptimization::DataCostFunctor{
                inline DataCostFunctorWrapper(FunctorT && f) : fun(std::forward<FunctorT>(f)) {}
                virtual GCoptimization::EnergyTermType compute(GCoptimization::SiteID s, GCoptimization::LabelID l) override {
                    return fun(s, l);
                }
                FunctorT fun;
            };
            template <class FunctorT>
            inline DataCostFunctorWrapper<FunctorT> * AllocDataCostFunctor(FunctorT && f) {
                return new DataCostFunctorWrapper<FunctorT>(std::forward<FunctorT>(f));
            }

            template <class FunctorT>
            struct SmoothCostFunctorWrapper : GCoptimization::SmoothCostFunctor {
                inline SmoothCostFunctorWrapper(FunctorT && f) : fun(std::forward<FunctorT>(f)){}
                virtual GCoptimization::EnergyTermType compute(
                    GCoptimization::SiteID s1, GCoptimization::SiteID s2,
                    GCoptimization::LabelID l1, GCoptimization::LabelID l2) override {
                    return fun(s1, s2, l1, l2);
                }
                FunctorT fun;
            };
            template <class FunctorT>
            inline SmoothCostFunctorWrapper<FunctorT> * AllocSmoothCostFunctor(FunctorT && f) {
                return new SmoothCostFunctorWrapper<FunctorT>(std::forward<FunctorT>(f));
            }


        }


        void ReconstructionEngine::initializeRegionOrientations() {

            std::vector<RegionIndex> regionIndices;
            std::map<RegionIndex, int> regionIndexToGraphSiteId;

            for (auto & vd : _views.elements<0>()){
                RegionIndex ri;
                ri.viewHandle = vd.topo.hd;
                for (auto & rd : vd.data.regionNet->regions().elements<0>()){
                    ri.handle = rd.topo.hd;
                    regionIndices.push_back(ri);
                    regionIndexToGraphSiteId[ri] = regionIndices.size() - 1;
                }
            }

            enum RegionOrientationType {
                VP0 = 0,
                VP1 = 1,
                VP2 = 2,
                //Planar = 3,
                //Other = 4,
                RegionOrientationTypeNum
            };

            GCoptimizationGeneralGraph graph(regionIndices.size(), RegionOrientationTypeNum);

            // set neighbors
            // region boundaries
            for (auto & vd : _views.elements<0>()){
                for (auto & bd : vd.data.regionNet->regions().elements<1>()){
                    RegionIndex ri1 = { vd.topo.hd, bd.topo.lowers[0] };
                    RegionIndex ri2 = { vd.topo.hd, bd.topo.lowers[1] };
                    auto siteId1 = regionIndexToGraphSiteId[ri1];
                    auto siteId2 = regionIndexToGraphSiteId[ri2];
                    graph.setNeighbors(siteId1, siteId2);
                }
            }
            // region overlaps
            for (auto & rp : _globalData.overlappedRegionIndexPairs){
                auto overlappingRatio = rp.second;
                if (overlappingRatio < 0.05)
                    continue;
                auto siteId1 = regionIndexToGraphSiteId[rp.first.first];
                auto siteId2 = regionIndexToGraphSiteId[rp.first.second];
                graph.setNeighbors(siteId1, siteId2);
            }

            
            // data costs for regions
            // store region costs corresponding to orientations
            IndexHashMap<RegionIndex, std::array<double, 3>> regionOrientationCosts;
            for (auto & ri : regionIndices){
                regionOrientationCosts[ri] = { { 0.0, 0.0, 0.0 } };
            }            
            for (auto & regionLine : _globalData.regionLineIntersectionSampledPoints){
                auto & ri = regionLine.first.first;
                auto & nearbyLi = regionLine.first.second;
                int nearbyLineClass = lineData(nearbyLi).line.claz;
                if (nearbyLineClass == -1)
                    continue;
                size_t sampledPointsNum = regionLine.second.size();
                regionOrientationCosts[ri][nearbyLineClass] += sampledPointsNum;
            }
            for (auto & c : regionOrientationCosts) {
                for (int i = 0; i < 3; i++) {
                    c.second[i] = 1.0 - Gaussian(c.second[i], 30.0);                    
                }
            }


            static const int scaleFactor = 100;

            graph.setDataCostFunctor(AllocDataCostFunctor([this, &regionIndices, &regionIndexToGraphSiteId, &regionOrientationCosts](
                GCoptimization::SiteID s, GCoptimization::LabelID l){
                // nearby lines
                auto & ri = regionIndices[s];
                if (l < 3){
                    return regionOrientationCosts[ri][l] * scaleFactor;
                }
                else{
                    NOT_IMPLEMENTED_YET();
                }
            }));


            // smooth costs for region relations

            // store region folding costs
            IndexHashMap<std::pair<RegionIndex, RegionIndex>, std::array<double, 3>> 
                regionFoldingCosts;
            for (auto & vd : _views.elements<0>()){
                HPoint2 vps[] = {
                    vd.data.camera.screenProjectionInHPoint(_globalData.vanishingPoints[0]),
                    vd.data.camera.screenProjectionInHPoint(_globalData.vanishingPoints[1]),
                    vd.data.camera.screenProjectionInHPoint(_globalData.vanishingPoints[2])
                };
                auto & regions = vd.data.regionNet->regions();
                for (auto & bd : regions.elements<1>()){
                    if (bd.data.sampledPoints.empty())
                        continue;

                    RegionIndex ri1 = { vd.topo.hd, bd.topo.lowers[0] };
                    RegionIndex ri2 = { vd.topo.hd, bd.topo.lowers[1] };
                    Point2 sampledPointsCenter(0, 0);
                    int num = 0;
                    for (auto & ps : bd.data.sampledPoints) {
                        for (auto & p : ps) {
                            sampledPointsCenter = sampledPointsCenter + p;
                            num++;
                        }
                    }
                    sampledPointsCenter /= num;
                    
                    for (int i = 0; i < 3; i++) {
                        Vec2 midToVP = vps[i] - HPoint2(sampledPointsCenter);
                        Vec2 edgeDir = bd.data.fittedLine.direction;
                        double angle = std::min(AngleBetweenDirections(midToVP, edgeDir), AngleBetweenDirections(midToVP, -edgeDir));
                        double cost = (1.0 - Gaussian(angle, M_PI / 32.0)) * 1 /** (1.0 - Gaussian(bd.data.length, 20.0))*/ 
                            * Gaussian(bd.data.straightness, 0.8);
                        regionFoldingCosts[std::make_pair(ri1, ri2)][i] = cost;
                        regionFoldingCosts[std::make_pair(ri2, ri1)][i] = cost;
                    }

                    std::cout << "region folding cost: " 
                        << regionFoldingCosts[std::make_pair(ri1, ri2)][0] << ", "
                        << regionFoldingCosts[std::make_pair(ri1, ri2)][1] << ", "
                        << regionFoldingCosts[std::make_pair(ri1, ri2)][2] << std::endl;
                }
            }



            graph.setSmoothCostFunctor(AllocSmoothCostFunctor([this, &regionIndices, &regionIndexToGraphSiteId, &regionFoldingCosts](
                GCoptimization::SiteID s1, GCoptimization::SiteID s2,
                GCoptimization::LabelID l1, GCoptimization::LabelID l2){
                auto & ri1 = regionIndices[s1];
                auto & ri2 = regionIndices[s2];
                if (ri1.viewHandle == ri2.viewHandle) { // region boundary
                    if (l1 < 3 && l2 < 3){ // folding
                        if (l1 == l2) { // same orientation cost
                            return 0; // TODO 
                        }
                        int foldOrientation = 0 + 1 + 2 - l1 - l2;
                        assert(regionFoldingCosts.find(std::make_pair(ri1, ri2)) != regionFoldingCosts.end());
                        return (int)regionFoldingCosts[std::make_pair(ri1, ri2)][foldOrientation] * 30 * scaleFactor;
                    }
                    else{
                        NOT_IMPLEMENTED_YET();
                    }
                }
                else { // region overlap
                    return l1 == l2 ? 0 : 10 * scaleFactor;
                }
            }));


            IF_DEBUG_USING_VISUALIZERS{

                // visualize data costs and folding costs
                for (auto & vd : _views.elements<0>()){                    

                    ImageWithType<Vec<uint8_t, 3>> orientationImage = ImageWithType<Vec<uint8_t, 3>>::zeros(vd.data.image.size());
                    for (int y = 0; y < vd.data.image.rows; y++){
                        for (int x = 0; x < vd.data.image.cols; x++){
                            auto regionId = vd.data.regionNet->segmentedRegions().at<int32_t>(PixelLoc(x, y));
                            RegionIndex ri = { vd.topo.hd, RegionsNet::RegionHandle(regionId) };
                            auto costs = regionOrientationCosts[ri];
                            Vec<uint8_t, 3> color(costs[0] * 255, costs[1] * 255, costs[2] * 255);
                            orientationImage(PixelLoc(x, y)) = color;
                        }
                    }

                    vis::Visualizer2D viz(orientationImage);

                    for (auto & bd : vd.data.regionNet->regions().elements<1>()){
                        RegionIndex ri1 = { vd.topo.hd, bd.topo.lowers[0] };
                        RegionIndex ri2 = { vd.topo.hd, bd.topo.lowers[1] };
                        auto costs = regionFoldingCosts[std::make_pair(ri1, ri2)];
                        vis::Color color(255 - costs[0] * 255, 255 - costs[1] * 255, 255 - costs[2] * 255);
                        auto & edges = bd.data.edges;
                        for (auto & e : edges){
                            for (int i = 0; i < e.size() - 1; i++){
                                viz.params.color = color;
                                viz.params.thickness = 2;
                                viz = viz << Line<int, 2>(e[i], e[i + 1]);
                            }
                        }
                    }

                    viz << vis::manip2d::Show();
                }

            }


            std::cout << "energy before graph-cut: " << graph.compute_energy() << std::endl;
            graph.expansion(50);
            graph.swap(50);
            std::cout << "energy after graph-cut: " << graph.compute_energy() << std::endl;

            for (int i = 0; i < regionIndices.size(); i++){
                _globalData.regionOrientations[regionIndices[i]] = graph.whatLabel(i);
            }

            IF_DEBUG_USING_VISUALIZERS{

                static const vis::Color colors[] = {
                    vis::ColorFromTag(vis::ColorTag::Red),
                    vis::ColorFromTag(vis::ColorTag::Green),
                    vis::ColorFromTag(vis::ColorTag::Blue),
                    vis::ColorFromTag(vis::ColorTag::Yellow),
                    vis::ColorFromTag(vis::ColorTag::White)
                };

                // visualize result region labels
                for (auto & vd : _views.elements<0>()) {
                    RegionIndex ri;
                    ri.viewHandle = vd.topo.hd;

                    auto & regions = *vd.data.regionNet;
                    int width = regions.segmentedRegions().cols;
                    int height = regions.segmentedRegions().rows;
                    ImageWithType<cv::Vec<uint8_t, 3>> coloredOutput = ImageWithType<cv::Vec<uint8_t, 3>>::zeros(height, width);
                    for (int y = 0; y < height; y++) {
                        for (int x = 0; x < width; x++) {
                            auto regionId = regions.segmentedRegions().at<int32_t>(cv::Point(x, y));
                            ri.handle.id = regionId;
                            auto & color = colors[_globalData.regionOrientations[ri]];
                            coloredOutput(cv::Point(x, y)) =
                                Vec<uint8_t, 3>((uint8_t)color[0], (uint8_t)color[1], (uint8_t)color[2]);
                        }
                    }

                    vis::Visualizer2D(coloredOutput) << vis::manip2d::Show();
                }

            }

        }















    }
}