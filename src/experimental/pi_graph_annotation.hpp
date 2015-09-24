#pragma once

#include "../core/serialization.hpp"
#include "../core/utility.hpp"
#include "pi_graph.hpp"

namespace pano {
    namespace experimental {

        // annotation
        struct AnnotedPolygon {
            Polygon3 polygon;
            SegControl control;
            template <class Archiver>
            inline void serialize(Archiver & ar) {
                ar(polygon, control);
            }
        };
        struct AnnotedOcclusion {
            Chain3 chain; // left is always front!
            template <class Archiver>
            inline void serialize(Archiver & ar) {
                ar(chain);
            }
        };

        struct PIAnnotation {
            Image originalImage;
            Image rectifiedImage;
            bool extendedOnTop;
            bool extendedOnBottom;

            PanoramicView view;
            std::vector<Vec3> vps;
            int vertVPId;
            std::vector<Classified<Line3>> lines;
            std::vector<AnnotedPolygon> polygons;
            std::vector<AnnotedOcclusion> occlusions;
            std::vector<Line3> foldingLines;

            PIAnnotation() : vertVPId(-1) {}

            template <class Archiver>
            inline void load(Archiver & ar, std::int32_t version) {
                if (version == 0) {
                    ar(originalImage, rectifiedImage, view, vps, vertVPId, lines, polygons, occlusions);
                } else if(version == 1){
                    ar(originalImage, rectifiedImage, view, vps, vertVPId, lines, polygons, occlusions, foldingLines);
                } else {
                    NOT_IMPLEMENTED_YET();
                }
            }

            template <class Archiver>
            inline void save(Archiver & ar, std::int32_t version) const {
                if (version == 0) {
                    ar(originalImage, rectifiedImage, view, vps, vertVPId, lines, polygons, occlusions);
                } else if (version == 1) {
                    ar(originalImage, rectifiedImage, view, vps, vertVPId, lines, polygons, occlusions, foldingLines);
                } else {
                    NOT_IMPLEMENTED_YET();
                }
            }
        };


        std::string AnnotationFilePath(const std::string & imagePath);

        PIAnnotation LoadOrInitializeNewAnnotation(const std::string & imagePath);

        void EditAnnotation(PIAnnotation & anno);

        void SaveAnnotation(const std::string & imagePath, const PIAnnotation & anno);

        // AttachAnnotatedPolygonsAndOcclusions
        void AttachAnnotatedPolygonsAndOcclusions(PIGraph & mg,
            const std::vector<AnnotedPolygon> & polygons, const std::vector<AnnotedOcclusion> & occs,
            double polygonBoundarySampleStepAngle = DegreesToRadians(1),
            double occChainSampleStepAngle = DegreesToRadians(0.1),
            double occChainToBndPieceAngleThres = DegreesToRadians(3));

    }
}


CEREAL_CLASS_VERSION(pano::experimental::PIAnnotation, 0);