#include "boundingViewHierarchy.h"
#include <stdlib.h>
#include <stdio.h>

// Internal helper functions (not exposed in the header file)
void calcBoundsbvh(NSVGshape ** fillShapes, BVHNode * node, int fillShapeCount){
  node->bounds[0] = fillShapes[0]->bounds[0]; //minx
  node->bounds[1] = fillShapes[0]->bounds[1]; //miny
  node->bounds[2] = fillShapes[0]->bounds[2]; //maxx
  node->bounds[3] = fillShapes[0]->bounds[3]; //maxy

  for(int i = 1; i < fillShapeCount; i++){
    NSVGshape * shape = fillShapes[i];
    if (shape->bounds[0] < node->bounds[0]) {node->bounds[0] = shape->bounds[0];};
    if (shape->bounds[1] < node->bounds[1]) {node->bounds[1] = shape->bounds[1];};
    if (shape->bounds[2] > node->bounds[2]) {node->bounds[2] = shape->bounds[2];};
    if (shape->bounds[3] > node->bounds[3]) {node->bounds[3] = shape->bounds[3];};
  }
}

int compareX(const void* a, const void* b) {
    NSVGshape* shapeA = *(NSVGshape**)a;
    NSVGshape* shapeB = *(NSVGshape**)b;
    float midA = (shapeA->bounds[0] + shapeA->bounds[2]) / 2;
    float midB = (shapeB->bounds[0] + shapeB->bounds[2]) / 2;
    if (midA < midB) return -1;
    if (midA > midB) return 1;
    return 0;
}

int compareY(const void* a, const void* b) {
    NSVGshape* shapeA = *(NSVGshape**)a;
    NSVGshape* shapeB = *(NSVGshape**)b;
    float midA = (shapeA->bounds[1] + shapeA->bounds[3]) / 2;
    float midB = (shapeB->bounds[1] + shapeB->bounds[3]) / 2;
    if (midA < midB) return -1;
    if (midA > midB) return 1;
    return 0;
}

int longestAxis(float bounds[4]){ //return 0 if X axis larger, return 1 if Y axis larger.
  return (bounds[2] - bounds[0]) < (bounds[3] - bounds[1]); //x > y
}

BVHNode* ConstructBVH(NSVGshape **fillShapes, int fillShapeCount, int depth) {
  //printf("ConstructBVH called with depth %d and fillShapeCount %d\n", depth, fillShapeCount);
  fflush(stdout);
  BVHNode* node = (BVHNode*) malloc(sizeof(BVHNode));
  if(node == NULL) {
    // Handle memory allocation failure
    printf("Memory allocation failure for BVHNode\n");
    fflush(stdout);
    return NULL;
  }
  node->left = NULL;
  node->right = NULL;
  node->shape = NULL;
  calcBoundsbvh(fillShapes, node, fillShapeCount);


  // If there's only one shape left, this is a leaf node.
  if (fillShapeCount == 1) {
    node->shape = fillShapes[0]; // Store the single shape directly
    return node;
  }

  // Choose the axis along which to split the shapes
  int axis = longestAxis(node->bounds); // This function will calculate the longest axis from the bounds

  // Sort the shapes along the chosen axis
  if(axis == 0){
    qsort(fillShapes, fillShapeCount, sizeof(NSVGshape*), compareX);
    //printf("Shapes sorted along the X axis\n");
  } else {
    qsort(fillShapes, fillShapeCount, sizeof(NSVGshape*), compareY);
    //printf("Shapes sorted along the Y axis\n");
  }
  fflush(stdout);

  // Split the list of shapes into two halves
  int midPoint = fillShapeCount / 2;

  // Create children nodes
  node->left = ConstructBVH(fillShapes, midPoint, depth + 1);
  node->right = ConstructBVH(fillShapes + midPoint, fillShapeCount - midPoint, depth + 1);

  //printf("Returning node at depth %d\n", depth);
  fflush(stdout);

  return node;
}

void writeBVHNodeToFile(BVHNode* node, FILE* file, int depth) {
    if (file == NULL) {
        printf("File pointer is NULL. Cannot write to file.\n");
        return;
    }

    if (node == NULL) {
        printf("Node pointer is NULL. Cannot write node information.\n");
        return;
    }

    // Write indentation corresponding to depth
    for (int i = 0; i < depth; i++) {
        fprintf(file, "\t");
    }

    // Write bounding box
    fprintf(file, "Node bounds: [%f, %f, %f, %f]", node->bounds[0], node->bounds[1], node->bounds[2], node->bounds[3]);

    // Write shape id if this is a leaf node
    if (node->shape != NULL) {
        fprintf(file, ", shape id: %s", node->shape->id);
    }

    fprintf(file, "\n");  // end of line
    fflush(file);  // force write to file to see output immediately

    // Recursively write children nodes
    if (node->left != NULL) {
        writeBVHNodeToFile(node->left, file, depth + 1);
    }
    if (node->right != NULL) {
        writeBVHNodeToFile(node->right, file, depth + 1);
    }
}

void FreeBVH(BVHNode* node) {
    if (!node) return;

    FreeBVH(node->left);
    FreeBVH(node->right);
    
    free(node);
}

void searchBVH(BVHNode *node, float x, float y, DynamicShapeArray *dynamicArray) {
    if (!node) return;

    // Check if the current node's bounding box contains the point
    if (x >= node->bounds[0] && x <= node->bounds[2] && y >= node->bounds[1] && y <= node->bounds[3]) {
        if (node->shape != NULL) {
            // If it's a leaf node and contains the point, add the shape to the dynamic array
            addToDynamicShapeArray(dynamicArray, node->shape);
        } else {
            // Otherwise, check both children
            searchBVH(node->left, x, y, dynamicArray);
            searchBVH(node->right, x, y, dynamicArray);
        }
    }
}