
extern "C" {
    #include <mosek.h>
}
#include <Eigen/Dense>
#include <Eigen/Sparse>

#include "algorithms.hpp"
#include "containers.hpp"
#include "mixed_graph.hpp"

namespace panoramix {
    namespace core {

        Plane3 MGUnaryVariable::interpretAsPlane() const {
            return Plane3FromEquation(variables[0], variables[1], variables[2]);
        }

        Line3 MGUnaryVariable::interpretAsLine(const MGUnary & line, const std::vector<Vec3> & vps) const {
            InfiniteLine3 infLine(line.normalizedCenter / variables[0], vps[line.lineClaz]);
            return Line3(DistanceBetweenTwoLines(InfiniteLine3(Point3(0, 0, 0), line.normalizedCorners.front()), infLine).second.second,
                DistanceBetweenTwoLines(InfiniteLine3(Point3(0, 0, 0), line.normalizedCorners.back()), infLine).second.second);
        }

        std::vector<double> MGUnaryVariable::variableCoeffsForInverseDepthAtDirection(const Vec3 & direction,
            const MGUnary & unary, const std::vector<Vec3> & vps) const{
            if (unary.type == MGUnary::Region){
                // depth = 1.0 / (ax + by + cz) where (x, y, z) = direction, (a, b, c) = variables
                // -> 1.0/depth = ax + by + cz
                return std::vector<double>{direction[0], direction[1], direction[2]};
            }
            else if (unary.type == MGUnary::Line){
                const auto & line = unary;
                InfiniteLine3 infLine(line.normalizedCenter, vps[line.lineClaz]);
                // variable is 1.0/centerDepth
                // corresponding coeff is 1.0/depthRatio
                // so that 1.0/depth = 1.0/centerDepth * 1.0/depthRatio -> depth = centerDepth * depthRatio
                double depthRatio = norm(DistanceBetweenTwoLines(InfiniteLine3(Point3(0, 0, 0), direction), infLine).second.first);
                return std::vector<double>{1.0 / depthRatio};
            }
        }

        double MGUnaryVariable::inverseDepthAtDirection(const Vec3 & direction,
            const MGUnary & unary, const std::vector<Vec3> & vps) const {
            if (unary.type == MGUnary::Region){
                // depth = 1.0 / (ax + by + cz) where (x, y, z) = direction, (a, b, c) = variables
                // -> 1.0/depth = ax + by + cz
                return variables[0] * direction[0] + variables[1] * direction[1] + variables[2] * direction[2];
            }
            else if (unary.type == MGUnary::Line){
                const auto & line = unary;
                InfiniteLine3 infLine(line.normalizedCenter, vps[line.lineClaz]);
                // variable is 1.0/centerDepth
                // corresponding coeff is 1.0/depthRatio
                // so that 1.0/depth = 1.0/centerDepth * 1.0/depthRatio -> depth = centerDepth * depthRatio
                double depthRatio = norm(DistanceBetweenTwoLines(InfiniteLine3(Point3(0, 0, 0), direction), infLine).second.first);
                return variables[0] / depthRatio; 
            }
        }

        double MGUnaryVariable::depthAtCenter(const MGUnary & unary, const std::vector<Vec3> & vps) const {
            return 1.0 / inverseDepthAtDirection(unary.normalizedCenter, unary, vps);
        }


        MixedGraph BuildMixedGraph(const std::vector<View<PerspectiveCamera>> & views,
            const std::vector<RegionsGraph> & regionsGraphs, const std::vector<LinesGraph> & linesGraphs,
            const ComponentIndexHashMap<std::pair<RegionIndex, RegionIndex>, double> & regionOverlappingsAcrossViews,
            const ComponentIndexHashMap<std::pair<LineIndex, LineIndex>, Vec3> & lineIncidencesAcrossViews,
            const std::vector<std::map<std::pair<RegionHandle, LineHandle>, std::vector<Point2>>> & regionLineConnections,
            const std::vector<Vec3> & vps,
            MGUnaryVarTable & unaryVars, MGBinaryVarTable& binaryVars,
            double initialDepth){

            MixedGraph mg;
            ComponentIndexHashMap<RegionIndex, MGUnaryHandle> ri2mgh;
            ComponentIndexHashMap<LineIndex, MGUnaryHandle> li2mgh;
            std::unordered_map<MGUnaryHandle, RegionIndex> mgh2ri;
            std::unordered_map<MGUnaryHandle, LineIndex> mgh2li;

            // add components in each view
            for (int i = 0; i < views.size(); i++){
                auto & cam = views[i].camera;
                // regions
                for (auto & rd : regionsGraphs[i].elements<0>()){
                    auto ri = RegionIndex{ i, rd.topo.hd };
                    std::vector<Vec3> normalizedContour;
                    for (auto & ps : rd.data.contours){
                        for (auto & p : ps){
                            normalizedContour.push_back(normalize(cam.spatialDirection(p)));
                        }
                    }
                    if (normalizedContour.size() <= 2){
                        continue;
                    }
                    auto center = normalize(cam.spatialDirection(rd.data.center));
                    int initialClaz = std::distance(vps.begin(), std::min_element(vps.begin(), vps.end(),
                        [&center](const Vec3 & vp1, const Vec3 & vp2) -> bool {
                        return AngleBetweenUndirectedVectors(center, vp1) <
                            AngleBetweenUndirectedVectors(center, vp2);
                    }));
                    ri2mgh[ri] = mg.add(MGUnary{
                        MGUnary::Region,
                        std::move(normalizedContour),
                        center,
                        -1
                    });
                    mgh2ri[ri2mgh[ri]] = ri;
                    int sign = center.dot(vps[initialClaz]) < 0 ? -1 : 1;
                    unaryVars[ri2mgh[ri]].variables = { 
                        sign * vps[initialClaz][0],
                        sign * vps[initialClaz][1], 
                        sign * vps[initialClaz][2] 
                    };
                    unaryVars[ri2mgh[ri]].fixed = false;
                }
                // lines
                for (auto & ld : linesGraphs[i].elements<0>()){
                    auto li = LineIndex{ i, ld.topo.hd };
                    li2mgh[li] = mg.add(MGUnary{
                        MGUnary::Line,
                        {
                            normalize(cam.spatialDirection(ld.data.line.component.first)),
                            normalize(cam.spatialDirection(ld.data.line.component.second))
                        },
                        normalize(cam.spatialDirection(ld.data.line.component.center())),
                        ld.data.line.claz
                    });
                    mgh2li[li2mgh[li]] = li;
                    unaryVars[li2mgh[li]].variables = { 1.0 };
                    unaryVars[li2mgh[li]].fixed = false;
                }
                // region-region in each view
                for (auto & bd : regionsGraphs[i].elements<1>()){
                    MGBinary rr;
                    rr.type = MGBinary::RegionRegionConnection;
                    rr.weight = 1.0;
                    rr.normalizedAnchors.reserve(bd.data.sampledPoints.size());
                    for (auto & ps : bd.data.sampledPoints){
                        for (auto & p : ps){
                            rr.normalizedAnchors.push_back(normalize(cam.spatialDirection(p)));
                        }
                    }
                    auto r1 = RegionIndex{ i, bd.topo.lowers.front() };
                    auto r2 = RegionIndex{ i, bd.topo.lowers.back() };
                    if (!Contains(ri2mgh, r1) || !Contains(ri2mgh, r2))
                        continue;
                    mg.add<1>({ ri2mgh[r1], ri2mgh[r2] }, std::move(rr));
                }
                // region-line
                for (auto & regionLine : regionLineConnections[i]){
                    MGBinary rl;
                    rl.type = MGBinary::RegionLineConnection;
                    rl.weight = 1.0;
                    rl.normalizedAnchors.reserve(regionLine.second.size());
                    for (auto & p : regionLine.second){
                        rl.normalizedAnchors.push_back(normalize(cam.spatialDirection(p)));
                    }
                    auto ri = RegionIndex{ i, regionLine.first.first };
                    auto li = RegionIndex{ i, regionLine.first.second };
                    if (!Contains(ri2mgh, ri))
                        continue;
                    mg.add<1>({ ri2mgh[ri], li2mgh[li] }, std::move(rl));
                }
                // line-line
                for (auto & rd : linesGraphs[i].elements<1>()){
                    auto l1 = LineIndex{ i, rd.topo.lowers.front() };
                    auto l2 = LineIndex{ i, rd.topo.lowers.back() };
                    if (rd.data.type == LineRelationData::Intersection){
                        MGBinary llinter;
                        llinter.type = MGBinary::LineLineIntersection;
                        llinter.weight = rd.data.junctionWeight * 10;
                        llinter.normalizedAnchors = { normalize(cam.spatialDirection(rd.data.relationCenter)) };
                        mg.add<1>({ li2mgh[l1], li2mgh[l2] }, std::move(llinter));
                    }
                    else if (rd.data.type == LineRelationData::Incidence){
                        MGBinary llincid;
                        llincid.type = MGBinary::LineLineIncidence;
                        llincid.weight = rd.data.junctionWeight * 10;
                        llincid.normalizedAnchors = { normalize(cam.spatialDirection(rd.data.relationCenter)) };
                        mg.add<1>({ li2mgh[l1], li2mgh[l2] }, std::move(llincid));
                    }
                }
            }
            // add cross view constraints
            // region-region overlappings
            for (auto & regionOverlapping : regionOverlappingsAcrossViews){
                if (regionOverlapping.second < 0.2)
                    continue;
                MGBinary rro;
                rro.type = MGBinary::RegionRegionOverlapping;
                rro.weight = 100;
                auto & r1 = regionOverlapping.first.first;
                auto & r2 = regionOverlapping.first.second;

                if (!Contains(ri2mgh, r1) || !Contains(ri2mgh, r2))
                    continue;

                // get samples
                Vec3 z = mg.data(ri2mgh[r1]).normalizedCenter + mg.data(ri2mgh[r2]).normalizedCenter;
                Vec3 x, y;
                std::tie(x, y) = ProposeXYDirectionsFromZDirection(z);
                double minx = std::numeric_limits<double>::max(),
                    miny = std::numeric_limits<double>::max();
                double maxx = std::numeric_limits<double>::lowest(),
                    maxy = std::numeric_limits<double>::lowest();
                rro.normalizedAnchors.resize(4, z);
                for (auto & a : mg.data(ri2mgh[r1]).normalizedCorners){
                    double dx = a.dot(x), dy = a.dot(y);
                    if (dx < minx){
                        rro.normalizedAnchors[0] = a;
                        minx = dx;
                    }
                    else if (dx > maxx){
                        rro.normalizedAnchors[1] = a;
                        maxx = dx;
                    }
                    if (dy < miny){
                        rro.normalizedAnchors[2] = a;
                        miny = dy;
                    }
                    else if (dy > maxy){
                        rro.normalizedAnchors[3] = a;
                        maxy = dy;
                    }
                }
                for (auto & a : mg.data(ri2mgh[r2]).normalizedCorners){
                    double dx = a.dot(x), dy = a.dot(y);
                    if (dx < minx){
                        rro.normalizedAnchors[0] = a;
                        minx = dx;
                    }
                    else if (dx > maxx){
                        rro.normalizedAnchors[1] = a;
                        maxx = dx;
                    }
                    if (dy < miny){
                        rro.normalizedAnchors[2] = a;
                        miny = dy;
                    }
                    else if (dy > maxy){
                        rro.normalizedAnchors[3] = a;
                        maxy = dy;
                    }
                }
                mg.add<1>({ ri2mgh[r1], ri2mgh[r2] }, std::move(rro));
            }
            // line-line incidencs
            for (auto & lineIncidence : lineIncidencesAcrossViews){
                MGBinary llincid;
                llincid.type = MGBinary::LineLineIncidence;
                llincid.weight = IncidenceJunctionWeight(true) * 10;
                llincid.normalizedAnchors = { normalize(lineIncidence.second) };
                auto & l1 = lineIncidence.first.first;
                auto & l2 = lineIncidence.first.second;
                mg.add<1>({ li2mgh[l1], li2mgh[l2] }, std::move(llincid));
            }

            // compute importance ratios
            std::vector<double> unaryWeightSums(mg.internalElements<0>().size(), 0.0);
            for (auto & b : mg.internalElements<1>()){
                unaryWeightSums[b.topo.lowers.front().id] += b.data.weight * b.data.normalizedAnchors.size();
                unaryWeightSums[b.topo.lowers.back().id] += b.data.weight * b.data.normalizedAnchors.size();
            }
            for (auto & b : mg.internalElements<1>()){
                b.data.importanceRatioInRelatedUnaries.front() = b.data.weight * b.data.normalizedAnchors.size() /
                    unaryWeightSums[b.topo.lowers.front().id];
                b.data.importanceRatioInRelatedUnaries.back() = b.data.weight * b.data.normalizedAnchors.size() /
                    unaryWeightSums[b.topo.lowers.back().id];
            }

#ifdef _DEBUG
            for (auto & u : mg.elements<0>()){
                double importanceRatioSum = 0.0;
                for (auto & bh : u.topo.uppers){
                    importanceRatioSum +=
                        mg.data(bh).importanceRatioInRelatedUnaries[u.topo.hd == mg.topo(bh).lowers[0] ? 0 : 1];
                }
                assert(FuzzyEquals(importanceRatioSum, 1.0, 0.01));
            }
#endif
            for (auto & b : mg.internalElements<1>()){
                binaryVars[b.topo.hd].enabled = true;
            }

            return mg;
        }





        bool BinaryHandlesAreValidInPatch(const MixedGraph & mg, const MGPatch & patch) {
            for (auto & bhv : patch.bhs){
                auto bh = bhv.first;
                if (!Contains(patch.uhs, mg.topo(bh).lowers.front()) ||
                    !Contains(patch.uhs, mg.topo(bh).lowers.back()))
                    return false;
            }
            return true;
        }

        bool UnariesAreConnectedInPatch(const MixedGraph & mg, const MGPatch & patch){
            std::unordered_map<MGUnaryHandle, bool> visited;
            visited.reserve(patch.uhs.size());
            for (auto & uhv : patch.uhs){
                visited[uhv.first] = false;
            }
            std::vector<MGUnaryHandle> uhs;
            uhs.reserve(patch.uhs.size());
            for (auto & uhv : patch.uhs){
                uhs.push_back(uhv.first);
            }
            BreadthFirstSearch(uhs.begin(), uhs.end(), [&mg, &patch](const MGUnaryHandle & uh){
                std::vector<MGUnaryHandle> neighbors;
                for (auto & bh : mg.topo(uh).uppers){
                    if (Contains(patch.bhs, bh)){
                        auto anotherUh = mg.topo(bh).lowers.front();
                        if (anotherUh == uh)
                            anotherUh = mg.topo(bh).lowers.back();
                        neighbors.push_back(anotherUh);
                    }
                }
                return neighbors;
            }, [&visited](MGUnaryHandle uh){
                visited[uh] = true;
                return true;
            });
            for (auto & v : visited){
                if (!v.second)
                    return false;
            }
            return true;
        }


        MGPatch MakePatchOnBinary(const MixedGraph & mg, const MGBinaryHandle & bh,
            const MGUnaryVarTable & unaryVars, const MGBinaryVarTable & binaryVars){
            MGPatch patch;
            patch.bhs[bh] = binaryVars.at(bh);
            auto uhs = mg.topo(bh).lowers;
            patch.uhs[uhs[0]] = unaryVars.at(uhs[0]);
            patch.uhs[uhs[1]] = unaryVars.at(uhs[1]);
            assert(BinaryHandlesAreValidInPatch(mg, patch));
            assert(UnariesAreConnectedInPatch(mg, patch));
            return patch;
        }


        MGPatch MakeStarPatchAroundUnary(const MixedGraph & mg, const MGUnaryHandle & uh,
            const MGUnaryVarTable & unaryVars, const MGBinaryVarTable & binaryVars){
            MGPatch patch;
            patch.uhs[uh] = unaryVars.at(uh);
            for (auto & bh : mg.topo(uh).uppers){
                auto anotherUh = mg.topo(bh).lowers.front();
                if (anotherUh == uh)
                    anotherUh = mg.topo(bh).lowers.back();
                patch.bhs[bh] = binaryVars.at(bh);
                patch.uhs[anotherUh] = unaryVars.at(uh);
            }
            assert(BinaryHandlesAreValidInPatch(mg, patch));
            assert(UnariesAreConnectedInPatch(mg, patch));
            return patch;
        }


        double AnchorDistanceSumOnBinaryOfPatch(const MixedGraph & mg, const MGBinaryHandle & bh, 
            const MGPatch & patch, const std::vector<Vec3> & vps){
            assert(Contains(patch, bh));
            auto uh1 = mg.topo(bh).lowers.front();
            auto uh2 = mg.topo(bh).lowers.back();
            double distanceSum = 0.0;
            for (auto & a : mg.data(bh).normalizedAnchors){
                distanceSum += abs(1.0 / patch.uhs.at(uh1).inverseDepthAtDirection(a, mg.data(uh1), vps) - 
                    1.0 / patch.uhs.at(uh2).inverseDepthAtDirection(a, mg.data(uh2), vps));
            }
            return distanceSum;
        }

        double BinaryDistanceOfPatch(const MixedGraph & mg, const MGBinaryHandle & bh,
            const MGPatch & patch, const std::vector<Vec3> & vps){
            assert(Contains(patch, bh));
            auto uh1 = mg.topo(bh).lowers.front();
            auto uh2 = mg.topo(bh).lowers.back();
            double distanceSum = 0.0;
            for (auto & a : mg.data(bh).normalizedAnchors){
                distanceSum += abs(1.0 / patch.uhs.at(uh1).inverseDepthAtDirection(a, mg.data(uh1), vps) -
                    1.0 / patch.uhs.at(uh2).inverseDepthAtDirection(a, mg.data(uh2), vps));
            }
            return distanceSum / mg.data(bh).normalizedAnchors.size();
        }

        double AverageBinaryDistanceOfPatch(const MixedGraph & mg,
            const MGPatch & patch, const std::vector<Vec3> & vps){
            double distanceSum = 0.0;
            for (auto & bhv : patch.bhs){
                distanceSum += BinaryDistanceOfPatch(mg, bhv.first, patch, vps);
            }
            return distanceSum / patch.bhs.size();
        }

        double AverageUnaryCenterDepthOfPatch(const MixedGraph & mg, const MGPatch & patch, const std::vector<Vec3> & vps){
            double depthSum = 0.0;
            for (auto & uhv : patch.uhs){
                depthSum += uhv.second.depthAtCenter(mg.data(uhv.first), vps);
            }
            return depthSum / patch.uhs.size();
        }



        std::vector<MGPatch> SplitMixedGraphIntoPatches(const MixedGraph & mg,
            const MGUnaryVarTable & unaryVars, const MGBinaryVarTable & binaryVars){

            std::unordered_map<MGUnaryHandle, int> ccids;
            std::vector<MGUnaryHandle> uhs;
            uhs.reserve(mg.internalElements<0>().size());
            for (auto & u : mg.elements<0>()){
                uhs.push_back(u.topo.hd);
            }
            int ccNum = core::ConnectedComponents(uhs.begin(), uhs.end(), [&mg](MGUnaryHandle uh){
                std::vector<MGUnaryHandle> neighbors;
                for (auto & bh : mg.topo(uh).uppers){
                    auto anotherUh = mg.topo(bh).lowers.front();
                    if (anotherUh == uh)
                        anotherUh = mg.topo(bh).lowers.back();
                    neighbors.push_back(anotherUh);
                }
                return neighbors;
            }, [&ccids](MGUnaryHandle uh, int ccid){
                ccids[uh] = ccid;
            });

            std::vector<MGPatch> patches(ccNum);
            for (auto & uhccid : ccids){
                auto uh = uhccid.first;
                int ccid = uhccid.second;
                patches[ccid].uhs[uh] = unaryVars.at(uh);
            }
            for (auto & b : mg.elements<1>()){
                auto & uhs = mg.topo(b.topo.hd).lowers;
                assert(ccids[uhs.front()] == ccids[uhs.back()]);
                patches[ccids[uhs.front()]].bhs[b.topo.hd] = binaryVars.at(b.topo.hd);
            }

            for (auto & p : patches){
                assert(UnariesAreConnectedInPatch(mg, p));
                assert(BinaryHandlesAreValidInPatch(mg, p));
            }

            return patches;
        }


        std::vector<MGPatch> SplitPatch(const MixedGraph & mg, const MGPatch & patch,
            std::function<bool(MGBinaryHandle bh)> useBh){

            std::vector<MGUnaryHandle> uhs;
            uhs.reserve(patch.uhs.size());
            for (auto & uhv : patch.uhs){
                uhs.push_back(uhv.first);
            }

            std::unordered_map<MGUnaryHandle, int> ccids;
            int ccNum = core::ConnectedComponents(uhs.begin(), uhs.end(), [&mg, &patch, &useBh](MGUnaryHandle uh){
                std::vector<MGUnaryHandle> neighbors;
                for (auto & bh : mg.topo(uh).uppers){
                    if (!Contains(patch.bhs, bh))
                        continue;
                    if (!useBh(bh))
                        continue;
                    auto anotherUh = mg.topo(bh).lowers.front();
                    if (anotherUh == uh)
                        anotherUh = mg.topo(bh).lowers.back();
                    neighbors.push_back(anotherUh);
                }
                return neighbors;
            }, [&ccids](MGUnaryHandle uh, int ccid){
                ccids[uh] = ccid;
            });

            std::vector<MGPatch> patches(ccNum);
            for (auto & uhccid : ccids){
                auto uh = uhccid.first;
                int ccid = uhccid.second;
                patches[ccid].uhs[uh] = patch.uhs.at(uh);
            }
            for (auto & b : mg.elements<1>()){
                auto & uhs = mg.topo(b.topo.hd).lowers;
                if (ccids[uhs.front()] == ccids[uhs.back()]){
                    patches[ccids[uhs.front()]].bhs[b.topo.hd] = patch.bhs.at(b.topo.hd);
                }
            }

            for (auto & p : patches){
                assert(UnariesAreConnectedInPatch(mg, p));
                assert(BinaryHandlesAreValidInPatch(mg, p));
            }

            return patches;
        }




        MGPatch MinimumSpanningTreePatch(const MixedGraph & mg, const MGPatch & patch,
            std::function<bool(MGBinaryHandle, MGBinaryHandle)> compareBh){

            assert(UnariesAreConnectedInPatch(mg, patch));
            assert(BinaryHandlesAreValidInPatch(mg, patch));

            MGPatch mst;
            mst.uhs = patch.uhs;

            std::vector<MGUnaryHandle> uhs;
            uhs.reserve(patch.uhs.size());
            for (auto & uhv : patch.uhs){
                uhs.push_back(uhv.first);
            }
            std::vector<MGBinaryHandle> bhs;
            bhs.reserve(patch.bhs.size());
            for (auto & bhv : patch.bhs){
                bhs.push_back(bhv.first);
            }

            std::vector<MGBinaryHandle> bhsReserved;

            core::MinimumSpanningTree(uhs.begin(), uhs.end(), bhs.begin(), bhs.end(),
                std::back_inserter(bhsReserved),
                [&mg](const MGBinaryHandle & bh){
                return mg.topo(bh).lowers;
            }, compareBh);

            for (auto & bh : bhsReserved){
                mst.bhs[bh] = patch.bhs.at(bh);
            }

            assert(UnariesAreConnectedInPatch(mg, mst));
            assert(BinaryHandlesAreValidInPatch(mg, mst));

            return mst;
        }




        namespace {


            std::vector<Vec3> NecessaryAnchorsForBinary(const MixedGraph & mg, MGBinaryHandle bh){
                auto & b = mg.data(bh);
                if (b.type == MGBinary::LineLineIncidence || b.type == MGBinary::LineLineIntersection)
                    return b.normalizedAnchors;
                if (b.type == MGBinary::RegionRegionOverlapping){
                    assert(b.normalizedAnchors.size() >= 3);
                    return { b.normalizedAnchors[0], b.normalizedAnchors[1], b.normalizedAnchors[2] };
                }
                if (b.type == MGBinary::RegionLineConnection){
                    return { b.normalizedAnchors.front(), b.normalizedAnchors.back() };
                }
                if (b.type == MGBinary::RegionRegionConnection){
                    Vec3 alignDir = normalize(b.normalizedAnchors.front().cross(b.normalizedAnchors.back()));
                    double maxDotProd = 0.0;
                    int maxOffsetedAnchorId = -1;
                    for (int k = 1; k < b.normalizedAnchors.size() - 1; k++){
                        double dotProd = abs(alignDir.dot(b.normalizedAnchors[k]));
                        if (dotProd > maxDotProd){
                            maxDotProd = dotProd;
                            maxOffsetedAnchorId = k;
                        }
                    }
                    if (maxDotProd > 1e-3){
                        return{ b.normalizedAnchors.front(), b.normalizedAnchors[maxOffsetedAnchorId], b.normalizedAnchors.back() };
                    }
                    else {
                        return{ b.normalizedAnchors.front(), b.normalizedAnchors.back() };
                    }
                }
                else{
                    return{};
                }
            }




            struct MGPatchDepthsOptimizerInternalBase {
                virtual void initialize(const MixedGraph & mg, MGPatch & patch,
                    const std::vector<Vec3> & vanishingPoints, bool useWeights) = 0;
                virtual bool optimize(const MixedGraph & mg, MGPatch & patch,
                    const std::vector<Vec3> & vanishingPoints) = 0;
                virtual void finalize() = 0;
            };


            struct MGPatchDepthsOptimizerInternalMosek : MGPatchDepthsOptimizerInternalBase {
                static struct Mosek {
                    MSKenv_t env;
                    inline Mosek() { MSK_makeenv(&env, nullptr); }
                    inline ~Mosek() { MSK_deleteenv(&env); }
                } mosek;
                static void MSKAPI printstr(void *handle,
                    MSKCONST char str[]){
                    printf("%s", str);
                }

                MSKtask_t task;
                std::unordered_map<MGUnaryHandle, int> uh2varStartPosition;
                std::unordered_map<MGBinaryHandle, int> bh2consStartPosition;
                std::unordered_map<MGBinaryHandle, std::vector<Vec3>> appliedBinaryAnchors;

                std::unordered_map<MGUnaryHandle, bool> uhFixed;
                std::map<int, std::vector<MGUnaryHandle>> uhCCs;
                std::unordered_map<MGUnaryHandle, int> uhCCIds;

                virtual void initialize(const MixedGraph & mg, MGPatch & patch,
                    const std::vector<Vec3> & vanishingPoints, bool useWeights){

                    assert(BinaryHandlesAreValidInPatch(mg, patch));
                    assert(UnariesAreConnectedInPatch(mg, patch));

                    assert(BinaryHandlesAreValidInPatch(mg, patch));
                    assert(UnariesAreConnectedInPatch(mg, patch));

                    
                    for (auto & uhv : patch.uhs){
                        uhFixed[uhv.first] = uhv.second.fixed;
                    }

                    // find cc with more than 3 necessary anchors
                    for (auto & bhv : patch.bhs){
                        if (!bhv.second.enabled)
                            continue;
                        auto bh = bhv.first;
                        auto uh1 = mg.topo(bh).lowers.front();
                        auto uh2 = mg.topo(bh).lowers.back();
                        auto & u1 = mg.data(uh1);
                        auto & u2 = mg.data(uh2);

                        bool u1IsFixed = !Contains(uh2varStartPosition, uh1);
                        bool u2IsFixed = !Contains(uh2varStartPosition, uh2);
                        if (u1IsFixed && u2IsFixed)
                            continue;

                        appliedBinaryAnchors[bhv.first] = NecessaryAnchorsForBinary(mg, bh);
                    }

                    
                    {
                        std::vector<MGUnaryHandle> uhs;
                        uhs.reserve(patch.uhs.size());
                        for (auto & uhv : patch.uhs){
                            uhs.push_back(uhv.first);
                        }
                        core::ConnectedComponents(uhs.begin(), uhs.end(),
                            [&mg, &patch, this](MGUnaryHandle uh) -> std::vector<MGUnaryHandle> {
                            std::vector<MGUnaryHandle> neighbors;
                            for (auto & bh : mg.topo(uh).uppers){
                                if (!Contains(appliedBinaryAnchors, bh))
                                    continue;
                                if (appliedBinaryAnchors.at(bh).size() != 3) // use only STRONG connections!!
                                    continue;
                                MGUnaryHandle anotherUh = mg.topo(bh).lowers[0];
                                if (anotherUh == uh)
                                    anotherUh = mg.topo(bh).lowers[1];
                                if (!Contains(patch, uh))
                                    continue;
                                neighbors.push_back(anotherUh);
                            }
                            return neighbors;
                        }, [this](MGUnaryHandle uh, int ccId){
                            uhCCs[ccId].push_back(uh);
                            uhCCIds[uh] = ccId;
                        });
                    }

                    // spread the fix status and assign the fixed variable
                    for (auto & cc : uhCCs){
                        auto & uhs = cc.second;
                        // collect fixed uhs in this cc
                        Vec3 abc(0, 0, 0);
                        int fixedNum = 0;
                        for (auto uh : uhs){
                            assert(mg.data(uh).type == MGUnary::Region);
                            auto & uhVar = patch.uhs.at(uh);
                            if (uhVar.fixed){
                                Vec3 thisAbc(uhVar.variables[0], uhVar.variables[1], uhVar.variables[2]);
                                if (Distance(abc / fixedNum, thisAbc) > 1e-2){
                                    std::cerr << "variables of fixed unaries do not match!!!!" << std::endl;
                                }
                                abc += thisAbc;
                                fixedNum++;
                            }
                        }
                        if (fixedNum == 0)
                            continue;
                        abc /= fixedNum;
                        for (auto uh : uhs){
                            auto & uhVar = patch.uhs.at(uh);
                            if (!uhVar.fixed){
                                uhVar.variables = { abc[0], abc[1], abc[2] };
                            }
                            uhFixed[uh] = true;
                        }
                    }



                    int varNum = 0;
                    for (auto & uhv : patch.uhs){
                        if (uhFixed.at(uhv.first)){
                            continue;
                        }
                        int ccId = uhCCIds.at(uhv.first);
                        auto firstUhInThisCC = uhCCs.at(ccId).front();
                        if (firstUhInThisCC == uhv.first){ // this is the first uh
                            uh2varStartPosition[uhv.first] = varNum;
                            varNum += uhv.second.variables.size();
                        }
                        else{
                            uh2varStartPosition[uhv.first] = uh2varStartPosition.at(firstUhInThisCC); // share the variables!!
                        }
                    }

                    int consNum = 0;
                    for (auto & bhv : patch.bhs){
                        if (!bhv.second.enabled)
                            continue;
                        auto bh = bhv.first;
                        auto uh1 = mg.topo(bh).lowers.front();
                        auto uh2 = mg.topo(bh).lowers.back();
                        auto & u1 = mg.data(uh1);
                        auto & u2 = mg.data(uh2);

                        bool u1IsFixed = uhFixed.at(uh1);
                        bool u2IsFixed = uhFixed.at(uh2);
                        if (u1IsFixed && u2IsFixed)
                            continue;

                        bh2consStartPosition[bhv.first] = consNum;
                        consNum += appliedBinaryAnchors[bhv.first].size();
                    }

                    int slackNum = consNum;
                    int realVarNum = varNum;

                    varNum += consNum; // real vars and slacks
                    consNum *= 2;   // [equation = 0] < slackVar;  
                                    // -[equation = 0] < slackVar
                    
                    task = nullptr;
                    auto & env = mosek.env;

                    MSK_maketask(env, consNum, varNum, &task);
                    MSK_linkfunctotaskstream(task, MSK_STREAM_LOG, NULL, printstr);

                    MSK_appendcons(task, consNum);
                    MSK_appendvars(task, varNum);

                    // set weights
                    {
                        int slackVarId = 0;
                        for (auto & bha : appliedBinaryAnchors){
                            auto & bd = mg.data(bha.first);
                            assert(bd.weight >= 0.0);
                            for (int k = 0; k < bha.second.size(); k++){
                                MSK_putcj(task, slackVarId, useWeights ? bd.weight : 1.0);
                                slackVarId++;
                            }
                        }
                    }

                    // bounds for vars
                    {
                        int varId = 0;
                        for (; varId < realVarNum; varId++){ // for depths
                            MSK_putvarbound(task, varId, MSK_BK_LO, 1.0, +MSK_INFINITY);
                        }
                    }

                    //// TODO!!
                    //// fill constraints related to each vars
                    //{
                    //    int varId = 0;
                    //    for (auto & uhv : patch.uhs){ // depths
                    //        auto & uh = uhv.first;
                    //        /*if (uhv.second.fixed)
                    //            continue;*/

                    //        auto & relatedBhs = mg.topo(uh).uppers;
                    //        int relatedAnchorsNum = 0;
                    //        for (auto & bh : relatedBhs){
                    //            if (!Contains(patch.bhs, bh))
                    //                continue;
                    //            if (!Contains(bh2consStartPosition, bh))
                    //                continue;
                    //            relatedAnchorsNum += appliedBinaryAnchors[bh].size();
                    //        }

                    //        int relatedConsNum = relatedAnchorsNum * 2;

                    //        std::vector<MSKint32t> consIds;
                    //        std::vector<MSKrealt> consValues;

                    //        consIds.reserve(relatedConsNum);
                    //        consValues.reserve(relatedConsNum);

                    //        for (auto & bh : relatedBhs){
                    //            if (!Contains(patch.bhs, bh))
                    //                continue;

                    //            int firstAnchorPosition = bh2consStartPosition.at(bh);
                    //            auto & samples = appliedBinaryAnchors.at(bh);
                    //            for (int k = 0; k < samples.size(); k++){
                    //                consIds.push_back((firstAnchorPosition + k) * 2); // one for [depth1 * ratio1 - depth2 * ratio2 - slackVar < 0]; 
                    //                consIds.push_back((firstAnchorPosition + k) * 2 + 1); // another for [- depth1 * ratio1 + depth2 * ratio2 - slackVar < 0];

                    //                auto & a = samples[k];
                    //                double ratio = DepthRatioOnMGUnary(a, mg.data(uh), vanishingPoints, patch.uhs.at(uh).claz);
                    //                bool isOnLeftSide = uh == mg.topo(bh).lowers.front();
                    //                if (isOnLeftSide){ // as depth1
                    //                    consValues.push_back(ratio);
                    //                    consValues.push_back(-ratio);
                    //                }
                    //                else{
                    //                    consValues.push_back(-ratio);
                    //                    consValues.push_back(ratio);
                    //                }
                    //            }
                    //        }

                    //        if (g_DepthBoundAsConstraint){ // add depth bound
                    //            consIds.push_back(samplesNum * 2 + varId);
                    //            consValues.push_back(1.0); // (1.0) * depth > 1.0;
                    //        }

                    //        assert(varId == uhPositions[uh]);
                    //        MSK_putacol(task, uhPositions[uh], relatedConsNum, consIds.data(), consValues.data());
                    //        varId++;
                    //    }

                    //    for (; varId < depthNum + slackVarNum; varId++){ // slack vars
                    //        MSKint32t consIds[] = {
                    //            (varId - depthNum) * 2, // one for [depth1 * ratio1 - depth2 * ratio2 - slackVar < 0]; 
                    //            (varId - depthNum) * 2 + 1  // another for [- depth1 * ratio1 + depth2 * ratio2 - slackVar < 0];
                    //        };
                    //        MSKrealt consValues[] = { -1.0, -1.0 };
                    //        MSK_putacol(task, varId, 2, consIds, consValues);
                    //    }
                    //}




                }
                virtual bool optimize(const MixedGraph & mg, MGPatch & patch,
                    const std::vector<Vec3> & vanishingPoints){
                    return false;
                }
                virtual void finalize() {}
            };

            MGPatchDepthsOptimizerInternalMosek::Mosek MGPatchDepthsOptimizerInternalMosek::mosek;


            struct MGPatchDepthsOptimizerInternalEigen : MGPatchDepthsOptimizerInternalBase {
                Eigen::SparseMatrix<double> A, W;
                Eigen::VectorXd B;
                bool useWeights;

                std::unordered_map<MGUnaryHandle, int> uh2varStartPosition;
                std::unordered_map<MGBinaryHandle, int> bh2consStartPosition;

                std::unordered_map<MGBinaryHandle, std::vector<Vec3>> appliedBinaryAnchors;

                virtual void initialize(const MixedGraph & mg, MGPatch & patch,
                    const std::vector<Vec3> & vanishingPoints, bool useWeights) override {

                    assert(BinaryHandlesAreValidInPatch(mg, patch));
                    assert(UnariesAreConnectedInPatch(mg, patch));

                    //std::unordered_map<MGUnaryHandle, bool> uhFixed;
                    //for (auto & uhv : patch.uhs){
                    //    uhFixed[uhv.first] = uhv.second.fixed;
                    //}

                    //// find cc with more than 3 necessary anchors
                    //for (auto & bhv : patch.bhs){
                    //    if (!bhv.second.enabled)
                    //        continue;
                    //    auto bh = bhv.first;
                    //    auto uh1 = mg.topo(bh).lowers.front();
                    //    auto uh2 = mg.topo(bh).lowers.back();
                    //    auto & u1 = mg.data(uh1);
                    //    auto & u2 = mg.data(uh2);

                    //    bool u1IsFixed = !Contains(uh2varStartPosition, uh1);
                    //    bool u2IsFixed = !Contains(uh2varStartPosition, uh2);
                    //    if (u1IsFixed && u2IsFixed)
                    //        continue;

                    //    appliedBinaryAnchors[bhv.first] = NecessaryAnchorsForBinary(mg, bh);
                    //}

                    //std::map<int, std::vector<MGUnaryHandle>> uhCCs;
                    //std::unordered_map<MGUnaryHandle, int> uhCCIds;
                    //{
                    //    std::vector<MGUnaryHandle> uhs;
                    //    uhs.reserve(patch.uhs.size());
                    //    for (auto & uhv : patch.uhs){
                    //        uhs.push_back(uhv.first);
                    //    }
                    //    core::ConnectedComponents(uhs.begin(), uhs.end(),
                    //        [&mg, &patch, this](MGUnaryHandle uh) -> std::vector<MGUnaryHandle> {
                    //        std::vector<MGUnaryHandle> neighbors;
                    //        for (auto & bh : mg.topo(uh).uppers){
                    //            if (!Contains(appliedBinaryAnchors, bh))
                    //                continue;
                    //            if (appliedBinaryAnchors.at(bh).size() != 3) // use only STRONG connections!!
                    //                continue;
                    //            MGUnaryHandle anotherUh = mg.topo(bh).lowers[0];
                    //            if (anotherUh == uh)
                    //                anotherUh = mg.topo(bh).lowers[1];
                    //            if (!Contains(patch, uh))
                    //                continue;
                    //            neighbors.push_back(anotherUh);
                    //        }
                    //        return neighbors;
                    //    }, [&uhCCs, &uhCCIds](MGUnaryHandle uh, int ccId){
                    //        uhCCs[ccId].push_back(uh);
                    //        uhCCIds[uh] = ccId;
                    //    });
                    //}

                    //// spread the fix status
                    //for ()



                    int varNum = 0;
                    bool hasFixedUnary = false;
                    for (auto & uhv : patch.uhs){
                        //if (uhFixed.at(uhv.first)){
                        if (uhv.second.fixed){
                            hasFixedUnary = true;
                            continue;
                        }
                        //int ccId = uhCCIds.at(uhv.first);
                        //auto firstUhInThisCC = uhCCs.at(ccId).front();
                        //if (firstUhInThisCC == uhv.first){ // this is the first uh
                            uh2varStartPosition[uhv.first] = varNum;
                            varNum += uhv.second.variables.size();
                        //}
                        //else{
                        //    uh2varStartPosition[uhv.first] = uh2varStartPosition.at(firstUhInThisCC); // share the variables!!
                        //}                        
                    }

                    int consNum = 0;

                    if (!hasFixedUnary){
                        consNum++;
                    }
                    for (auto & bhv : patch.bhs){
                        if (!bhv.second.enabled)
                            continue;
                        auto bh = bhv.first;
                        auto uh1 = mg.topo(bh).lowers.front();
                        auto uh2 = mg.topo(bh).lowers.back();
                        auto & u1 = mg.data(uh1);
                        auto & u2 = mg.data(uh2);

                        bool u1IsFixed = !Contains(uh2varStartPosition, uh1);
                        bool u2IsFixed = !Contains(uh2varStartPosition, uh2);
                        if (u1IsFixed && u2IsFixed)
                            continue;

                        bh2consStartPosition[bhv.first] = consNum;
                        appliedBinaryAnchors[bhv.first] = NecessaryAnchorsForBinary(mg, bh);
                        consNum += appliedBinaryAnchors[bhv.first].size();
                    }

                    A.resize(consNum, varNum);
                    W.resize(consNum, consNum);
                    B.resize(consNum);

                    A.reserve(consNum * 6);
                    W.reserve(consNum);

                    // write equations
                    int eid = 0;
                    if (!hasFixedUnary){ // the anchor constraint
                        MGUnaryHandle uh = patch.uhs.begin()->first;
                        auto & uhVar = patch.uhs.begin()->second;
                        int uhVarNum = uhVar.variables.size();
                        Vec3 uhCenter = mg.data(uh).normalizedCenter;
                        auto uhVarCoeffsAtCenter = uhVar.variableCoeffsForInverseDepthAtDirection(uhCenter, mg.data(uh), vanishingPoints);
                        assert(uhVarCoeffsAtCenter.size() == uhVar.variables.size());
                        int uhVarStartPosition = uh2varStartPosition.at(uh);
                        for (int i = 0; i < uhVarCoeffsAtCenter.size(); i++){
                            A.insert(eid, uhVarStartPosition + i) = uhVarCoeffsAtCenter[i];
                        }
                        B(eid) = 1.0;
                        W.insert(eid, eid) = 1.0;
                        eid++;
                    }
                    for (auto & bhv : patch.bhs){
                        if (!bhv.second.enabled)
                            continue;

                        auto & bh = bhv.first;
                        auto uh1 = mg.topo(bh).lowers.front();
                        auto uh2 = mg.topo(bh).lowers.back();
                        auto & u1 = mg.data(uh1);
                        auto & u2 = mg.data(uh2);

                        bool u1IsFixed = !Contains(uh2varStartPosition, uh1);
                        bool u2IsFixed = !Contains(uh2varStartPosition, uh2);

                        if (u1IsFixed && u2IsFixed){
                            continue;
                        }

                        int u1VarStartPosition = u1IsFixed ? -1 : uh2varStartPosition.at(uh1);
                        auto & u1Var = patch.uhs.at(uh1);
                        int u1VarNum = u1Var.variables.size();

                        int u2VarStartPosition = u2IsFixed ? -1 : uh2varStartPosition.at(uh2);
                        auto & u2Var = patch.uhs.at(uh2);
                        int u2VarNum = u2Var.variables.size();                        

                        for (auto & a : appliedBinaryAnchors.at(bh)){ 

                            B(eid) = 0.0;
                            assert(mg.data(bh).weight >= 0.0);
                            W.insert(eid, eid) = mg.data(bh).weight;

                            if (u1IsFixed){
                                double inverseDepthAtA = u1Var.inverseDepthAtDirection(a, u1, vanishingPoints);
                                B(eid) = - inverseDepthAtA;
                            }
                            else{
                                auto u1VarCoeffs = u1Var.variableCoeffsForInverseDepthAtDirection(a, u1, vanishingPoints);
                                assert(u1VarCoeffs.size() == u1VarNum);
                                for (int i = 0; i < u1VarCoeffs.size(); i++){
                                    A.insert(eid, u1VarStartPosition + i) = u1VarCoeffs[i]; // pos
                                }
                            }

                            if (u2IsFixed){
                                double inverseDepthAtA = u2Var.inverseDepthAtDirection(a, u2, vanishingPoints);
                                B(eid) = inverseDepthAtA;
                            }
                            else{
                                auto u2VarCoeffs = u2Var.variableCoeffsForInverseDepthAtDirection(a, u2, vanishingPoints);
                                assert(u2VarCoeffs.size() == u2VarNum);
                                for (int i = 0; i < u2VarCoeffs.size(); i++){
                                    A.insert(eid, u2VarStartPosition + i) = -u2VarCoeffs[i]; // neg
                                }
                            }
                           
                            eid++;
                        }
                    }
                    assert(eid == consNum);

                }

                virtual bool optimize(const MixedGraph & mg, MGPatch & patch,
                    const std::vector<Vec3> & vanishingPoints)  override {

                    using namespace Eigen;

                    SparseQR<Eigen::SparseMatrix<double>, COLAMDOrdering<int>> solver;
                    static_assert(!(Eigen::SparseMatrix<double>::IsRowMajor), "COLAMDOrdering only supports column major");
                    Eigen::SparseMatrix<double> WA = W * A;
                    A.makeCompressed();
                    WA.makeCompressed();

                    solver.compute(useWeights ? WA : A);

                    if (solver.info() != Success) {
                        assert(0);
                        std::cout << "computation error" << std::endl;
                        return false;
                    }

                    VectorXd WB = W * B;
                    VectorXd X = solver.solve(useWeights ? WB : B);
                    if (solver.info() != Success) {
                        assert(0);
                        std::cout << "solving error" << std::endl;
                        return false;
                    }

                    for (auto & uhv : patch.uhs){
                        if (!Contains(uh2varStartPosition, uhv.first))
                            continue;
                        int uhStartPosition = uh2varStartPosition.at(uhv.first);
                        for (int i = 0; i < uhv.second.variables.size(); i++){
                            uhv.second.variables[i] = X(uhStartPosition + i);
                        }
                    }

                    return true;
                }
                virtual void finalize() {}
            };

        }


        MGPatchDepthsOptimizer::MGPatchDepthsOptimizer(const MixedGraph & mg, MGPatch & patch,
            const std::vector<Vec3> & vanishingPoints, bool useWeights, Algorithm at)
            : _mg(mg), _patch(patch), _vanishingPoints(vanishingPoints), _at(at){

            if (_at == Algorithm::MosekLinearProgramming){
                _internal = new MGPatchDepthsOptimizerInternalMosek;
            }
            else if (_at == Algorithm::EigenSparseQR){
                _internal = new MGPatchDepthsOptimizerInternalEigen;
            }

            auto internalData = static_cast<MGPatchDepthsOptimizerInternalBase*>(_internal);
            internalData->initialize(mg, patch, vanishingPoints, useWeights);
        }

        MGPatchDepthsOptimizer::MGPatchDepthsOptimizer(MGPatchDepthsOptimizer && pdo)
            :_at(pdo._at), _mg(pdo._mg), _patch(pdo._patch), _vanishingPoints(pdo._vanishingPoints) {
            _internal = pdo._internal;
            pdo._internal = nullptr;
        }



        MGPatchDepthsOptimizer::~MGPatchDepthsOptimizer(){
            if (_internal){
                auto internalData = static_cast<MGPatchDepthsOptimizerInternalBase*>(_internal);
                internalData->finalize();
                delete internalData;
            }
        }


        bool MGPatchDepthsOptimizer::optimize() {
            return static_cast<MGPatchDepthsOptimizerInternalBase*>(_internal)->optimize(_mg, _patch, _vanishingPoints);
        }



   	}
}