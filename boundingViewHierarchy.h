#ifndef BVH_H
#define BVH_H

#ifndef NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#endif

#include "stdio.h"

typedef struct BVHNode {
    float bounds[4];       // Bounding box of this node [minx, miny, maxx, maxy]
    NSVGshape* shape;      // Shape at this node (NULL if not a leaf node)
    struct BVHNode* left;  // Left child
    struct BVHNode* right; // Right child
} BVHNode;

BVHNode* ConstructBVH(NSVGshape **fillShapes, int fillShapeCount, int depth);
void FreeBVH(BVHNode* node);
void writeBVHNodeToFile(BVHNode* node, FILE* file, int depth);

#endif // BVH_H
