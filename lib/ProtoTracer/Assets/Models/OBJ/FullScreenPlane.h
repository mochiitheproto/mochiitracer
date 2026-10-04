#pragma once

#include "../../../Scene/Objects/Object3D.h"
#include "../../../Scene/Materials/Static/SimpleMaterial.h"

// Simple full-screen quad covering the face camera view (0,0) to (192,94) at z=0.
// Used to display flat images like BSOD at full coverage regardless of 3D face morph state.
class FullScreenPlane {
private:
    Vector3D basisVertices[4] = {
        Vector3D(  0.0f,  0.0f, 0.0f),
        Vector3D(192.0f,  0.0f, 0.0f),
        Vector3D(192.0f, 94.0f, 0.0f),
        Vector3D(  0.0f, 94.0f, 0.0f)
    };
    IndexGroup basisIndexes[2] = { IndexGroup(0, 1, 2), IndexGroup(0, 2, 3) };
    StaticTriangleGroup<4, 2> triangleGroup = StaticTriangleGroup<4, 2>(&basisVertices[0], &basisIndexes[0]);
    TriangleGroup<4, 2> triangleGroupMemory = TriangleGroup<4, 2>(&triangleGroup);
    SimpleMaterial defaultMaterial = SimpleMaterial(RGBColor(0, 0, 0));
    Object3D basisObj = Object3D(&triangleGroup, &triangleGroupMemory, &defaultMaterial);

public:
    FullScreenPlane() {}

    Object3D* GetObject() { return &basisObj; }

    void Reset() { basisObj.ResetVertices(); }
};
