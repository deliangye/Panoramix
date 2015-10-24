#pragma once

#include "../misc/matlab_api.hpp"
#include "pi_graph.hpp"
#include "pi_graph_annotation.hpp"
#include "pi_graph_cg.hpp"

namespace pano {
    namespace experimental {
        
        void ReconstructLayoutAnnotation(PILayoutAnnotation & anno, misc::Matlab & matlab);

        // use general plane representation method
        void ReconstructLayoutAnnotation2(PILayoutAnnotation & anno, misc::Matlab & matlab);
        void ReconstructLayoutAnnotation3(PILayoutAnnotation & anno, misc::Matlab & matlab);



        double Solve(const PICGDeterminablePart & dp, PIConstraintGraph & cg, misc::Matlab & matlab, 
            int maxIter = std::numeric_limits<int>::max());
        int DisableUnsatisfiedConstraints(const PICGDeterminablePart & dp, PIConstraintGraph & cg, 
            const std::function<bool(double distRankRatio, double avgDist, double maxDist)> & whichToDisable);

        int DisorientLines(const PICGDeterminablePart & dp, const PIConstraintGraph & cg, PIGraph & mg);
        int DisorientSegs(const PICGDeterminablePart & dp, const PIConstraintGraph & cg, PIGraph & mg);


        Imaged DepthMap(const PICGDeterminablePart & dp, const PIConstraintGraph & cg, const PIGraph & mg);





        // get the CompactModel
        std::vector<Polygon3> CompactModel(const PICGDeterminablePart & dp, const PIConstraintGraph & cg, const PIGraph & mg);


    }
}