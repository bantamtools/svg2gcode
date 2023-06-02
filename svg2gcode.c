/*
 * svg2gcode (c) Matti Koskinen 2014
 *
 * reorder-function based on StippleGen
 * Copyright (C) 2012 by Windell H. Oskay, www.evilmadscientist.com
 *
 * nanosvg.h (c) 2014 Mikko Mononen
 * some routines based on nanosvg-master example1.c
 *
 * Xgetopt used on VS2010 (or later) by Hans Dietrich,David Smith
 * code public domain
 *
 * No comments :-)
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * http://creativecommons.org/licenses/LGPL/2.1/
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include <stdio.h>
#ifdef _MSC_VER
#include "XGetopt.h"
#else
#include <unistd.h>
#endif
#include <string.h>
#include <float.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include "svg2gcode.h"
#include <math.h>

//#define TESTRNG // remove if on linux or osx
//#define DO_HPGL //remove comment if you want to get a HPGL-code
#define DEBUG_GCODE //comment out for production
#define SHAPE_INTERSECTION //for debug purposes of shape detection.
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define GHEADER "G90\nG0 M3 S%d\nG55\n" //G92 X0 Y0\n //add here your specific G-codes. We want to use the G55 offset setting by default. This will hold a z-offset for caliration around the -2 default draw height.
#define GHEADER_NEW "nG90\n" //G92 X0 Y0\n //add here your specific G-codes
#define CUTTERON "G0 M3 S%d\n"
#define CUTTEROFF "M5\n" // same for this
#define GFOOTER "M5\nM30\n "
#define GMODE "M4\n"

static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }
static int numTools = 6;
static float bounds[4];
static int pathCount,pointsCount,shapeCount;
static int doBez = 1;
static struct NSVGimage* g_image = NULL;
int numCompOut = 0;
int pathCountOut = 0;
int pointCountOut = 0;

#ifdef SHAPE_INTERSECTION
typedef struct BVHNode { //Used for constructing a Bounding Volume Heirarchy for overlap search optimization.
    float bounds[4]; // Bounding box of this node [minx, miny, maxx, maxy]
    NSVGshape* shape; // Shape at this node (NULL if this is not a leaf node). Shapes are contained in leaf nodes, non leaf nodes are considered a minimum bounding box of their children. 
    struct BVHNode* left; // Left child
    struct BVHNode* right; // Right child
} BVHNode;
#endif

typedef struct {
  float x;
  float y;
} SVGPoint;

typedef struct {
  int *colors;
  int count;
  int slot;
} Pen;

typedef struct {
  float points[8];
  int city;
  char closed;
} ToolPath;

typedef struct {
  int id; //Z-Index. Higher Z indexes drawn above lower. 
  NSVGpaint stroke;
} City;

static SVGPoint bezPoints[64];
static SVGPoint first,last;
static int bezCount = 0;
#ifdef _WIN32

static uint64_t seed;

static int32_t rand31() {
    uint64_t tmp1;
    uint32_t tmp2;

    /* x = (16807 * x) % 0x7FFFFFFF */
    tmp1 = (uint64_t) ((int32_t) seed * (int64_t) 16807);
    tmp2 = (uint32_t) tmp1 & (uint32_t) 0x7FFFFFFF;
    tmp2 += (uint32_t) (tmp1 >> 31);
    if ((int32_t) tmp2 < (int32_t) 0)
      tmp2 = (tmp2 + (uint32_t) 1) & (uint32_t) 0x7FFFFFFF;
    return (int32_t)tmp2;
}
static void seedrand(float seedval) {
  seed = (int32_t) ((double) seedval + 0.5);
  if (seed < 1L) {                   /* seed from current time */
    seed = time(NULL);
    seed = ((seed - 1UL) % 0x7FFFFFFEUL) + 1UL;
  }
  else {
      seed = ((seed - 1L) % 0x7FFFFFFEL) + 1L;
    }
    seed = rand31();
    seed = rand31();
}

static double drnd31() {
  double x;
  seed = rand31();
  x = (double)(seed-0x3FFFFFFFL) * (2.0 / 1073741823.015625);
  if(fabs(x) > 1.0)
    x = drnd31();
  return fabs(x);
}
#endif

double distanceBetweenPoints(double x1, double y1, double x2, double y2){
  //printf("Distance between points, x1: %f, y1: %f, x2: %f, y2: %f\n", x1, y1, x2, y2);
  double result;
  double dx2 = (x2 - x1)*(x2 - x1);
  double dy2 = (y2 - y1)*(y2 - y1);
  result = sqrt(dx2 + dy2);
  //printf("Distance between points: %f\n", result);
  return result;
}

static float distPtSeg(float x, float y, float px, float py, float qx, float qy)
{
  float pqx, pqy, dx, dy, d, t;
  pqx = qx-px;
  pqy = qy-py;
  dx = x-px;
  dy = y-py;
  d = pqx*pqx + pqy*pqy;
  t = pqx*dx + pqy*dy;
  if (d > 0) t /= d;
  if (t < 0) t = 0;
  else if (t > 1) t = 1;
  dx = px + t*pqx - x;
  dy = py + t*pqy - y;
  return dx*dx + dy*dy;
}
// bezier smoothing
static void cubicBez(float x1, float y1, float x2, float y2,
             float x3, float y3, float x4, float y4,
             float tol, int level)
{
  float x12,y12,x23,y23,x34,y34,x123,y123,x234,y234,x1234,y1234;
  float d;

  if (level > 12) return;

  x12 = (x1+x2)*0.5f;
  y12 = (y1+y2)*0.5f;
  x23 = (x2+x3)*0.5f;
  y23 = (y2+y3)*0.5f;
  x34 = (x3+x4)*0.5f;
  y34 = (y3+y4)*0.5f;
  x123 = (x12+x23)*0.5f;
  y123 = (y12+y23)*0.5f;
  x234 = (x23+x34)*0.5f;
  y234 = (y23+y34)*0.5f;
  x1234 = (x123+x234)*0.5f;
  y1234 = (y123+y234)*0.5f;

  d = distPtSeg(x1234, y1234, x1,y1, x4,y4);
  if (d > tol*tol) {
    cubicBez(x1,y1, x12,y12, x123,y123, x1234,y1234, tol, level+1);
    cubicBez(x1234,y1234, x234,y234, x34,y34, x4,y4, tol, level+1);
  } else {
    bezPoints[bezCount].x = x4;
    bezPoints[bezCount].y = y4;
    bezCount++;
    if(bezCount > 63) {
      printf("Bez curve recursion limit!\n");
      bezCount = 63;
    }
  }
}
//#define TESTRNG
#ifdef _WIN32 //win doesn't have good RNG
#define RANDOM() drnd31() //((double)rand()/(double)RAND_MAX)
#else //OSX LINUX much faster than win
#define RANDOM() (drand48())
#endif


static int pcomp(const void* a, const void* b) {
  SVGPoint* ap = (SVGPoint*)a;
  SVGPoint* bp = (SVGPoint*)b;
  if(sqrt(ap->x*ap->x + ap->y*ap->y) > sqrt(bp->x*bp->x+bp->y*bp->y)) {
    return 1;
  }
  return -1;
}

// get all paths and paths into cities
static void calcPaths(SVGPoint* points, ToolPath* paths, int *npaths, City *cities) {
  struct NSVGshape* shape;
  struct NSVGpath* path;
  FILE *f;
  int i,j,k,l,p,b,bezCount, z_index;
  SVGPoint* pts;
#ifdef DO_HPGL
  f=fopen("test.hpgl","w");
  fprintf(f,"IN;SP1;");
#endif
  bezCount=0;
  i=0;
  k=0;
  j=0;
  p=0;
  for(shape = g_image->shapes; shape != NULL; shape=shape->next) { //city index == z_index
    for(path = shape->paths; path != NULL; path=path->next) {
      cities[i].id = i;
      cities[i].stroke = shape->stroke;
      //printf("City number %d color = %d\n", i, (shape->stroke.color));
      for(j=0;j<path->npts-1;(doBez ? j+=3 : j++)) {
        float *pp = &path->pts[j*2];
        if(j==0) {//add first two points. this is for lines and not bezier paths.
        points[i].x = pp[0];
        points[i].y = pp[1];
#ifdef DO_HPGL
        fprintf(f,"PU%d,%d;",(int)pp[0],(int)pp[1]);
        fflush(f);
          } else {
        fprintf(f,"PD%d,%d;",(int)pp[0],(int)pp[1]);
        fflush(f);
#endif
          }
        if(doBez) { //if we are doing bezier points, this will be reached and add the bezier points.
          bezCount++;
          //printf("DoBez in calcPaths. Bez#%d\n", bezCount);
          for(b=0;b<8;b++){
            paths[k].points[b]=pp[b];
          }
        } else {
          paths[k].points[0] = pp[0];
          paths[k].points[1] = pp[1];
          paths[k].points[2] = pp[0];
          paths[k].points[3] = pp[1];
        }
        paths[k].closed = path->closed;
        paths[k].city = i; //assign points in this path/shape to city i.
        k++;
       }
     cont:
       if(k>pointsCount) {
     printf("error k > \n");
#ifdef DO_HPGL
     fprintf(f,"PU0,0;\n");
     fclose(f);
#endif
     *npaths = 0;
     return;
       }
       if(i>pathCount) {
     printf("error i > \n");
#ifdef DO_HPGL
     fprintf(f,"PU0,0;\n");
     fclose(f);
#endif
     exit(-1);
       }
       i++; //setting up cities
     }
     j++;
  }
  printf("total paths %d, total points %d\n",i,k);
  *npaths = k;
#ifdef DO_HPGL
  fprintf(f,"PU0,0;\n");
  fclose(f);
#endif
}

//submethod for mergeSort
void merge(City * arr, int left, int mid, int right) {
    int n1 = mid - left + 1;
    int n2 = right - mid;

    City *leftArr = (City *) malloc(n1 * sizeof(City));
    City *rightArr = (City *) malloc(n2 * sizeof(City));
    memset(leftArr, 0, n1 * sizeof(City));
    memset(rightArr, 0, n2 * sizeof(City));

    for (int i = 0; i < n1; i++) {
        leftArr[i] = arr[left + i];
    }
    for (int i = 0; i < n2; i++) {
        rightArr[i] = arr[mid + 1 + i];
    }

    int i = 0, j = 0, k = left;
    while (i < n1 && j < n2) {
        if (leftArr[i].stroke.color <= rightArr[j].stroke.color) {
            arr[k] = leftArr[i];
            i++;
        } else {
            arr[k] = rightArr[j];
            j++;
        }
        k++;
    }

    while (i < n1) {
        arr[k] = leftArr[i];
        i++;
        k++;
    }

    while (j < n2) {
        arr[k] = rightArr[j];
        j++;
        k++;
    }

    free(leftArr);
    free(rightArr);
}

//sub array implementation of merge sort for sorting cities by color
void mergeSort(City * arr, int left, int right, int level, int* mergeLevel) {
  if(level > *mergeLevel){
    printf("Merge Sort level: %d\n", level);
    *mergeLevel = level;
  }
  if (left < right) {
    int mid = left + (right - left) / 2;
    mergeSort(arr, left, mid, level+1, mergeLevel);
    mergeSort(arr, mid + 1, right, level+1, mergeLevel);
    merge(arr, left, mid, right);
  }
}

int colorInPen(Pen pen, int color, int colorCount){
  for(int i = 0; i < colorCount; i++){
    if(pen.colors[i] == color){
      return 1;
    }
  }
  return 0;
}

//calculate the svg space bounds for the image and create initial city sized list of colors.
static void calcBounds(struct NSVGimage* image, int numTools, Pen *penList, int penColorCount[6])
{
  struct NSVGshape* shape;
  struct NSVGpath* path;
  int i;
  int colorMatch = 0;
  bounds[0] = FLT_MAX;
  bounds[1] = FLT_MAX;
  bounds[2] = -FLT_MAX;
  bounds[3] = -FLT_MAX;
  pathCount = 0;
  pointsCount = 0;
  shapeCount = 0;
  for (shape = image->shapes; shape != NULL; shape = shape->next) { //for all shapes in an image. Color is at this level
    for (path = shape->paths; path != NULL; path = path->next) { //for all path's in a shape. Path's inherit their shape color.
      for (i = 0; i < path->npts-1; i++) { //for all points in a path.
        float* p = &path->pts[i*2];
        bounds[0] = minf(bounds[0], p[0]);
        bounds[1] = minf(bounds[1], p[1]);
        bounds[2] = maxf(bounds[2], p[0]);
        bounds[3] = maxf(bounds[3], p[1]);
        pointsCount++;
      }
      pathCount++;
    }
    //add to penList[n] here based on color.
    for(int c = 0; c < numTools; c++){
        if(colorInPen(penList[c], shape->stroke.color, penColorCount[c])){ //need an inPenColors here. Take a pen and a color int and count of colors to pen. 1 if color in pen.
          penList[c].count++;
          colorMatch =1;
          continue;
        }
      }
    if(colorMatch ==0){ //if no color match was found add to tool 1
      penList[0].count++;
    } else {
      colorMatch =0;
    }
    shapeCount++;
  }
  printf("pathCount = %d\n", pathCount);
  printf("shapeCount = %d\n",shapeCount);
}

//sort array by color defined int.
int colorComp(const City * a, const City * b) {
  const City *A = a, *B = b;
  int x = A->stroke.color, y = B->stroke.color;
  return (x > y) - (x < y);
}


//need to set up indicies for each color to reorder between, as opposed to reordering the entire list.
//reorder the paths to minimize cutter movement. //default is xy = 1
static void reorder(SVGPoint* pts, int pathCount, char xy, City* cities, Pen* penList) {
  int i,j,k,temp1,temp2,indexA,indexB, indexH, indexL;
  City temp;
  float dx,dy,dist,dist2, dnx, dny, ndist, ndist2;
  SVGPoint p1,p2,p3,p4;
  SVGPoint pn1,pn2,pn3,pn4;
  //printf("Path count = %i\n", pathCount);
  int numComp = floor(sqrt(pointsCount));
  //int numComp = 10;
  //printf("numComp = %i\n",numComp);
  for(i=0;i<numComp*pathCount;i++) {
    //printf("Reorder i = %i\n", i);
    indexA = (int)(RANDOM()*(pathCount-2));
    indexB = (int)(RANDOM()*(pathCount-2));
    if(abs(indexB-indexA) < 2){
      continue;
    }
    if(indexB < indexA) { //work from left index a and right index b.
      temp1 = indexB;
      indexB = indexA;
      indexA = temp1;
    }
    pn1 = pts[cities[indexA].id];
    pn2 = pts[cities[indexA+1].id];
    pn3 = pts[cities[indexB].id];
    pn4 = pts[cities[indexB+1].id];
    dnx = pn1.x-pn2.x;
    dny = pn1.y-pn2.y;
    if(xy) {
      ndist = dnx * dnx + dny * dny;
    } else {
      ndist = dny * dny;
    }
    dnx = pn3.x-pn4.x;
    dny = pn3.y-pn4.y;

    if(xy) {
      ndist += (dnx * dnx + dny * dny);
    } else {
      ndist += dny * dny;
    }
    dnx = pn1.x - pn3.x;
    dny = pn1.y - pn3.y;
    if(xy){
      ndist2 = dnx * dnx + dny * dny;
    } else {
      ndist2 = dny * dny;
    }
    dnx = pn2.x - pn4.x;
    dny = pn2.y - pn4.y;
    if(xy) {
      ndist2 += dnx * dnx + dny * dny;
    } else {
      ndist2 += dny * dny;
    }
    if(ndist2 < ndist) {
      indexH = indexB;
      indexL = indexA+1;
      while(indexH > indexL) { //test cities swap.
        temp = cities[indexL];
        cities[indexL]=cities[indexH];
        cities[indexH] = temp;
        indexH--;
        indexL++;
      }
    }
  }
  pathCountOut = pathCount;
  pointCountOut = pointsCount;
  numCompOut = numComp;
}

#ifdef SHAPE_INTERSECTION
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
  printf("ConstructBVH called with depth %d and fillShapeCount %d\n", depth, fillShapeCount);
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

  printf("Bounds calculated: [%f, %f, %f, %f]\n", node->bounds[0], node->bounds[1], node->bounds[2], node->bounds[3]);
  fflush(stdout);

  // If there's only one shape left, this is a leaf node.
  if (fillShapeCount == 1) {
    node->shape = fillShapes[0]; // Store the single shape directly
    printf("Leaf node created with shape\n");
    fflush(stdout);
    return node;
  }

  // Choose the axis along which to split the shapes
  int axis = longestAxis(node->bounds); // This function will calculate the longest axis from the bounds
  printf("Longest axis calculated: %d\n", axis);
  fflush(stdout);

  // Sort the shapes along the chosen axis
  if(axis == 0){
    qsort(fillShapes, fillShapeCount, sizeof(NSVGshape*), compareX);
    printf("Shapes sorted along the X axis\n");
  } else {
    qsort(fillShapes, fillShapeCount, sizeof(NSVGshape*), compareY);
    printf("Shapes sorted along the Y axis\n");
  }
  fflush(stdout);

  // Split the list of shapes into two halves
  int midPoint = fillShapeCount / 2;
  printf("Midpoint calculated: %d\n", midPoint);
  fflush(stdout);

  // Create children nodes
  printf("Creating left child\n");
  fflush(stdout);
  node->left = ConstructBVH(fillShapes, midPoint, depth + 1);
  printf("Creating right child\n");
  fflush(stdout);
  node->right = ConstructBVH(fillShapes + midPoint, fillShapeCount - midPoint, depth + 1);

  printf("Returning node at depth %d\n", depth);
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
    if (node->shape != NULL && node->shape->id > -1) {
        fprintf(file, ", shape id: %d", node->shape->id);
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
#endif


void help() {
  printf("usage: svg2gcode [options] svg-file gcode-file\n");
  printf("options:\n");
  printf("\t-Y shift Y-ax\n");
  printf("\t-X shift X-ax\n");
  printf("\t-f feed rate (3500)\n");
  printf("\t-n # number of reorders (30)\n");
  printf("\t-s scale (1.0)\n");
  printf("\t-S 1 scale to material size\n");
  printf("\t-C center on drawing space\n");
  printf("\t-F flip Y-axis\n");
  printf("\t-w final width in mm\n");
  printf("\t-t Bezier tolerance (0.5)\n");
  printf("\t-m machine accuracy (0.1)\n");
  printf("\t-Z z-engage (-1.0)\n");
  printf("\t-B do Bezier curve smoothing\n");
  printf("\t-T engrave only TSP-path\n");
  printf("\t-V optmize for Voronoi Stipples\n");
  printf("\t-h this help\n");
  }

//want to rewrite the definition to contain integer values in one array, and float values in another so I don't have to keep passing more and more arguments.
//machineType 0 = 6-Color, 1 = LFP, 2 = MVP.
int generateGcode(int argc, char* argv[], int** penColors, int penColorCount[6], float paperDimensions[6], int scaleToMaterial, int centerSvg, int machineType) {
  printf("In Generate GCode\n");
  int i,j,k,l,first = 1;
  SVGPoint* points;
  ToolPath* paths;
  City *cities;
  //Shape detection specific variables
  #ifdef SHAPE_INTERSECTION
  int fillShapeCount = 0;
  NSVGshape * iterShape;
  NSVGshape ** fillShapes;
  BVHNode * bvhRoot;
  #endif
  //all 6 tools will have their color assigned manually. If a path has a color not set in p1-6, assign to p1 by default.
  Pen *penList; //counts each color occurrence + the int assigned. (currently, assign any unknown/unsupported to p1. sum of set of pX should == nPaths;)
  //int numTools = 6;
  int npaths;
  int feed = 13000;
  int slowTravel = 3500;
  int cityStart=1;
  float zFloor = paperDimensions[4]; //Pen draw height
  float ztraverse = paperDimensions[4] + paperDimensions[5]; //Travel height between moves
  float width = -1;
  float height =-1;
  char xy = 1;
  float w,h,widthInmm,heightInmm = -1.;
  int numReord = 10;
  float scale = 1; //make this dynamic. //this changes with widthInmm
  float margin = paperDimensions[2]; //xmargin around drawn elements in mm
  float ymargin = paperDimensions[3]; //ymargin around drawn elements in mm
  int fitToMaterial =  scaleToMaterial;
  int centerOnMaterial = centerSvg;
  int currColor = 1; //if currColor == 1, then no tool is currently being held.
  int targetColor = 0;
  int targetTool = 0; //start as 0 so no tool is matched
  int currTool = -1; //-1 indicates no tool picked up
  int colorMatch = 0;
  float toolChangePos = -51.5;
  float tol = 1; //smaller is better
  float accuracy = 0.05; //smaller is better
  float x,y,bx,by,bxold,byold,firstx,firsty;
  double d;
  float xold,yold;
  int flip = 1; //may want to pull out.
  int printed=0;
  int tsp = 0;
  int tspFirst = 1;
  int autoshift = 0;
  float ashift = 0.;
  int firstandonly = 2; // first svg
  int append = 0;
  int last = 0; // last svg
  int waitTime = 25;
  float maxx = -1000.,minx=1000.,maxy = -1000.,miny=1000.,zmax = -1000.,zmin = 1000;
  float shiftX = 0.;
  float shiftY = 0;
  float yMountOffset = 0; //mm
  float zeroX = 0.;
  float zeroY = 0.;
  FILE *gcode;
  int pwr = 90;
  int ch;
  int dwell = -1;
  char gbuff[128];
  printf("v0.0001 8.11.2020\n");
  //seed48(NULL);

  printf("Argc:%d\n", argc);

  if(argc < 3) {
    help();
    return -1;
  }
  while((ch=getopt(argc,argv,"D:ABhf:n:s:Fz:Z:S:w:t:m:cTV1aLP:CY:X:")) != EOF) {
    switch(ch) {
    case 'P': pwr = atoi(optarg);
      break;
    case 'D': waitTime=atoi(optarg);
      dwell = atoi(optarg);
      break;
    case 'a': append = 1; firstandonly = 0;
      break;
    case 'V': xy = 0;
      break;
    xy = 1;
      break;
    case 'Y': shiftY = atof(optarg); // shift
      break;
    case 'X': shiftX = atof(optarg); // shift
      break;
    case 'A':autoshift = 1;
      break;
    case 'm': accuracy=atof(optarg);
      break;
    case 'h': help();
      break;
    case 'f': feed = atoi(optarg);
      break;
    case 'n': numReord = atoi(optarg);
      break;
    case 's': scale = atof(optarg);
      break;
    case 't': tol = atof(optarg);
      break;
    case 'F':
      flip = 1;
      break;
    case 'w': widthInmm = atof(optarg);
      break;
    default: help();
      return(1);
      break;
    }
  }
  //move above to main
  if(shiftY != 30. && flip == 1)
    shiftY = -shiftY;
  printf("File open string: %s\n", argv[optind]);
  printf("File output string: %s\n", argv[optind+1]);
  g_image = nsvgParseFromFile(argv[optind],"px",96);
  if(g_image == NULL) {
    printf("error: Can't open input %s\n",argv[optind]);
    return -1;
  }
  fflush(stdout);

  //Bank of pens, their slot and their color. Pens also track count of cities to be drawn with their color (for debug purposes)
  penList = (Pen*)malloc(numTools*sizeof(Pen));
  memset(penList, 0, numTools*sizeof(Pen));
  //assign pen colors for penColors input
  for(int i = 0; i<numTools;i++){
    //printf("Tool %d in penColors color: %d\n", i, penColors[i]);
    penList[i].colors = penColors[i]; //assign penList[i].colors to the pointer passed in from penColors (there are numtools poiners to assign.)
  }

  calcBounds(g_image, numTools, penList, penColorCount);
  printf("Color counts:\n");
  for(int c = 0; c<numTools;c++){
    printf("\tp%d=%d\n",c,penList[c].count);
  }
  fflush(stdout);

  //Pre-proc for shape collision detection. Only want to detect for collision on shapes with fill.
  #ifdef SHAPE_INTERSECTION
  for (iterShape = g_image->shapes; iterShape != NULL; iterShape = iterShape->next) {
    //printf("Fill color == %i\n", iterShape->fill.color);
    if(iterShape->fill.color != 0){
      fillShapeCount++;
    }
  }
  printf("Number of shapes with fill: %i\n", fillShapeCount);
  fflush(stdout);

  FILE* debug = fopen("debug.txt", "w");
  if (debug == NULL) {
      printf("Failed to open file.\n");
  }
  printf("Debug.txt created\n");
  fflush(stdout);

  fillShapes = malloc(fillShapeCount * sizeof(NSVGshape*));
  if (fillShapes == NULL) {
      printf("Failed to malloc fillShapes.\n Exiting process\n");
      fflush(stdout);
      return -1;
  }

  shapeCount = 0;
  int shapeInsert = 0;
  printf("Constructing array of shapes with fill\n");
  fflush(stdout);
  for (iterShape = g_image->shapes; iterShape != NULL; iterShape = iterShape->next) { //Construct array of all shapes with fill in the image. For pre-proc purposes.
    if(iterShape->fill.color != 0){
      printf("Fill color when building fill arr: %i\n", iterShape->fill.color);
      fflush(stdout);
      fprintf(debug, "Fill Shape %i, Shape #%i: Bounds %f %f %f %f. Fill: %i\n", shapeInsert, shapeCount, iterShape->bounds[0], iterShape->bounds[1], iterShape->bounds[2], iterShape->bounds[3], iterShape->fill.color);
      fflush(debug);
      iterShape->id = shapeCount;
      fillShapes[shapeInsert] = iterShape;
      shapeInsert++;
    }
    shapeCount++;
  }

  //fillShapeCount is total num of shapes with fill. shapeCount is total number of shapes. fillShapes is an array of the shapes with fill.

  printf("Constructing bvhTree\n");
  fflush(stdout);

  for (int i = 0; i < fillShapeCount; i++) {
    for (int j = i+1; j < fillShapeCount; j++) {
      if (fillShapes[i] == fillShapes[j]) {
          printf("Duplicate shape found at indices %d and %d: %p\n", i, j, (void*)fillShapes[i]);
      }
    }
  } 


  bvhRoot = ConstructBVH(fillShapes, fillShapeCount, 0);
  printf("bvhTree constructed\n");
  fflush(stdout);

  writeBVHNodeToFile(bvhRoot, debug, 0);

  fflush(stdout);
  fclose(debug);
  #endif

  fprintf(stderr,"bounds %f %f X %f %f\n",bounds[0],bounds[1],bounds[2],bounds[3]);
  width = g_image->width;
  height = g_image->height;
  printf("Image width x height: %f x %f\n", width, height);

  //bounding box dimensions of drawn svg elements. Toggling between these different settings of w and h may be the change we want.
  //px * width/height. This is the width x height of the svg. Why are paths being drawn to this size?
  w = width;
  h = height; 
  printf("w x h: %f x %f\n", w, h);

  //scaling + fitting operations.
  float drawSpaceWidth = paperDimensions[0]-(2*margin); //space available on paper for drawing.
  float drawSpaceHeight = paperDimensions[1]-(2*ymargin);
  float drawingWidth = w; //size of drawing scaled. Just setting as placeholder for now.
  float drawingHeight = h;

  //Scale to material with default margin of 1"

  if((drawingWidth > drawSpaceHeight) || (drawingHeight > drawSpaceHeight)){ //if larger than material, force scale to material
    printf("SVG larger than material, forcing scale to material\n");
    fitToMaterial = 1;
  }

  if(fitToMaterial == 1){ //this can also increase the size. This is the key difference between fittomaterial == 1 and else {}
    printf("Fitting to material size\n");

    //need to identify the bounding dimension.
    float materialRatio = drawSpaceWidth/drawSpaceHeight;
    float svgRatio = w/h;
    //if materialRatio > svgRatio, Y is the bounding dimension. if material ratio is less than svgRatio, X is the bounding dimension.
    if(materialRatio > svgRatio){ //if y is bounding
      printf("Scaling to drawSpaceHeight = %f \n",drawSpaceHeight);
      scale = drawSpaceHeight/h;
      drawingWidth = w*scale;
      drawingHeight = h*scale;
      printf("scale = %f \n", drawSpaceWidth);
    } else if (svgRatio >= materialRatio){ //if x is bounding or equal
      printf("Scaling to drawSpaceWidth = %f \n",drawSpaceWidth);
      scale = drawSpaceWidth/w;
      drawingHeight = h*scale;
      drawingWidth = w*scale;
      printf("scale = %f \n", scale);
    }
    shiftX = margin;
    shiftY = ymargin;
  }
  if(centerOnMaterial == 1){ //rethink for based on x or y bound
    printf("Centering on drawing space\n");
    float centerX = drawingWidth/2;
    shiftX = margin + ((drawSpaceWidth/2) - (drawingWidth/2));
    shiftY = ymargin + ((drawSpaceHeight/2) - (drawingHeight/2));
  }

  // shiftX = 0;
  // shiftY = 0;

  printf("ShiftX:%f, ShiftY:%f\n", shiftX, shiftY);

  fprintf(stderr,"width  %f w %f scale %f width in mm %f\n",width,w,scale,widthInmm);
  fprintf(stderr,"height  %f h %f scale %f\n",width,h,scale);
  //offsetting by the -min x and miny, which would move it over the amount. Think this is why 
  //the drawing is not respecting viewbox attributes.
  zeroX = 0;//-bounds[0];
  zeroY = 0;//-bounds[1];

#ifdef _WIN32
seedrand((float)time(0));
#endif

 gcode=fopen(argv[optind+1],"w");
 if(gcode == NULL) {
   printf("can't open output %s\n",argv[optind+1]);
   return -1;
 }
  //fprintf(gcode, "w x h: %f x %f\n", w, h);
  printf("paths %d points %d\n",pathCount, pointsCount);
  // allocate memory
  points = (SVGPoint*)malloc(pathCount*sizeof(SVGPoint));
  paths = (ToolPath*)malloc(pointsCount*sizeof(ToolPath));
  cities = (City*)malloc(pathCount*sizeof(City));
  memset(points, 0, pathCount*sizeof(SVGPoint));
  memset(paths, 0, pointsCount*sizeof(ToolPath));
  memset(cities, 0, pathCount*sizeof(City));

  printf("Size of City: %lu, size of cities: %lu\n", sizeof(City), sizeof(City)*pathCount);
  npaths = 0;
  calcPaths(points, paths, &npaths, cities);

  printf("Reorder with numCities: %d\n",pathCount);
  for(k=0;k<numReord;k++) {
    reorder(points, pathCount, xy, cities, penList);
    printf("%d... ",k);
    fflush(stdout);
  }
  printf("\n");

  //If cities are reordered by distances first, using a stable sort after for color should maintain the sort order obtained by distances, but organized by colors.
  printf("Sorting cities by color\n");
  int mergeCount = 0;
  int* mergeLevel = &mergeCount; 
  mergeSort(cities, 0, pathCount-1, 0, mergeLevel); //this is stable and can be called on subarrays. So we want to reorder, then call on subarrays indexed by our mapped colors.

  // for(i = 0; i<pathCount; i++){
  //   printf("City %d at i:%d, Color:%d\n", cities[i].id, i, cities[i].stroke.color);
  // }

  double totalDist = 0;

  printf("\n");
  if(first) {
    fprintf(gcode,GHEADER,pwr);
    if(machineType == 0 || machineType == 2) { //6Color or MVP
      fprintf(gcode, "G1 Y0 F%i\n", feed);
      fprintf(gcode, "G1 Y%f F%i\n", -1*paperDimensions[1], feed);
      fprintf(gcode, "G1 Y0 F%i\n", feed);
    }
    //fprintf(gcode,"G92X0Y0Z0\n");
  }

  k=0;
  i=0;

  for(i=0;i<pathCount;i++) { //equal to the number of cities. Iterate through all paths. Mark it as the start of a city.
    cityStart=1;
    for(k=0;k<npaths;k++){ //npaths == number of points/ToolPaths in path. Looks at the city for each toolpath, and if it is equal to the city in this position's id
                            //in cities, then it beigs the print logic. This can almost certainly be optimized because each city does not have npaths paths associated.
      if(paths[k].city == -1){ //means already written
          continue;
      } else if(paths[k].city == cities[i].id) { //Time to write to file.
        break;
      }
    }
    if(k >= npaths-1) { //Next iteration.
      continue;
    }
    

    //colorCheck and tracking for TOOLCHANGE
    if(cityStart ==1 && (machineType == 0)){ //City start and 6Color
      targetColor = cities[i].stroke.color;
      if(targetColor != currColor) { //Detect tool slot of new color
        for(int p = 0; p<numTools; p++){
          if(colorInPen(penList[p], targetColor, penColorCount[p])){
          //if(penList[p].color == targetColor){
            targetTool = p;
            break;
          }
          targetTool = 0;// if none of the tools matched this will always set target tool to default tool.
        }
      }
      if(targetTool != currTool){ //need to check if tool picked up previously or not
        if(currTool >= 0){ //tool is being held
          //fprintf(gcode, "( Tool change needed to tool %d )\n",targetTool+1);
          //add pickup and dropoff logic
          fprintf(gcode, "G1 A%d\n", currTool*60); //rotate to current color slot
          fprintf(gcode, "G1 Z%i F%i\n", 0, feed);
          fprintf(gcode, "G0 X0\n"); //rapid move to close to tool changer
          fprintf(gcode, "G1 X%f F%i\n", toolChangePos, slowTravel); //slow move to dropoff
          fprintf(gcode, "G1 X0 F%d\n", slowTravel); //slow move away from dropoff
          fprintf(gcode, "G1 A%d\n", targetTool*60); //rotate to target slot
          fprintf(gcode, "G0 X0\n"); //rapid move to close to tool changer
          fprintf(gcode, "G1 X%f F%i\n", toolChangePos, slowTravel); //slow move to pickup
          fprintf(gcode, "G1 X0 F%i\n", slowTravel); //slow move away from pickup
          //fprintf(gcode, "( Tool change finished )\n");
          currTool = targetTool;
        }
        if(currTool == -1){ //no tool picked up
          currColor = penList[targetTool].colors[0];
          //fprintf(gcode,"( Tool change with no previous tool to tool %d )\n", targetTool+1);
          fprintf(gcode, "G1 A%d\n", targetTool*60); //rotate to target
          fprintf(gcode, "G1 Z%i F%i\n", 0, feed);
          fprintf(gcode, "G0 X0\n"); //rapid move to close to tool changer
          fprintf(gcode, "G1 X%f F%d\n", toolChangePos ,slowTravel); //slow move to pickup
          fprintf(gcode, "G1 X0 F%d\n", slowTravel); //slow move away from pickup
          //fprintf(gcode, "( Tool change finished )\n");
          currTool = targetTool;
        }
      }
    }
    //TOOLCHANGE END

    //Begin writing move.
    firstx = x = (paths[k].points[0]+zeroX)*scale+shiftX; //Starting x and y for this path/city.
    firsty = y =  (paths[k].points[1]+zeroY)*scale+shiftY;
    if(flip) {
      firsty = -firsty;
      y = -y;
    } if(x > maxx){
      maxx = x;
    } if(x < minx){
      minx = x;
    } if(y > maxy){
      maxy = y;
    } if(y < miny){
      miny = y;
    }

    fprintf(gcode, "G1 Z%f F%d\n",ztraverse,feed);
    #ifdef DEBUG_GCODE
    fprintf(gcode, "First in city.\n"); //Only write from this segment at start of city. Z-index of a city is == to its ID.
    #endif
    fprintf(gcode,"G0 X%.4f Y%.4f\n",x,y); //City segment starts here. Have determination for if segment is covered here, and use in conjunction with city start to decide when/if to lower/write.  Only write segment if not covered.
    //start of city. want to have first move in a city+lower here.
    fprintf(gcode,"( city %d, color %d)\n", cities[i].id, cities[i].stroke.color);
    if(cityStart ==1){
          fprintf(gcode, "G1 Z%f F%d\n",zFloor,feed);
          cityStart = 0;
    }
    printed=0;
    for(j=k;j<npaths;j++) { //Entire city iterated through here.
      xold = x;
      yold = y;
      first = 1;
      if(paths[j].city == cities[i].id) {
        bezCount = 0;
        cubicBez(paths[j].points[0],paths[j].points[1],paths[j].points[2],paths[j].points[3],paths[j].points[4],paths[j].points[5],paths[j].points[6],paths[j].points[7],tol,0); //Write all the bezier curve points to bezPoints.
        bxold=x;
        byold=y;
        for(l=0;l<bezCount;l++) {
          if(bezPoints[l].x > bounds[2] || bezPoints[l].x < bounds[0] || isnan(bezPoints[l].x)) {
            printf("bezPoints %f %f\n",bezPoints[l].x,bounds[0]);
            continue;
          }
          if(bezPoints[l].y > bounds[3]) {
            printf("bezPoints y %d\n",l);
            continue;
          }
          bx = (bezPoints[l].x+zeroX)*scale+shiftX; //Prepare to write x and y.
          by = (bezPoints[l].y+zeroY)*scale+shiftY;
          if(flip){
             by = -by;
          }
          if(bx > maxx) {
            maxx = bx;
          }
          if(bx < minx) {
            minx = x;
          }
          if(by > maxy) {
            maxy = by;
          }
          if(y < miny){
            miny = by;
          }
          //printf("Distance before: %f\n", d);
          d = distanceBetweenPoints(bxold, byold, bx, by);
          //printf("Distance after: %f\n", d);
          totalDist += d;
          printed = 1;
          //fprintf(gcode, "City:%d at i:%d=  ", cities[i].id, i);
          #ifdef DEBUG_GCODE
          fprintf(gcode, "Writing line-segment move\n"); //Seems most writing of segments happens here.
          #endif
          fprintf(gcode,"G1 X%.4f Y%.4f  F%d\n",bx,by,feed);
          bxold = bx;
          byold = by;
        }
      paths[j].city = -1; //this path has been written
      } else
            break;
    }
    if(paths[j].closed) {
      fprintf(gcode, "( end )\n");
      fprintf(gcode, "G1 Z%f F%d\n",ztraverse,feed);
      #ifdef DEBUG_GCODE
      fprintf(gcode, "Writing line-segment move on if[j] == closed \n"); //Final of city happens here.
      #endif 
      fprintf(gcode,"G1 X%.4f Y%.4f  F%d\n",firstx,firsty,feed);
      printed = 1;
    }
  }
  fprintf(gcode, "G1 Z%i F%i\n", 0, feed);
  //drop off current tool
  //TOOLCHANGE START
  if(machineType == 0){ //6Color
    fprintf(gcode, "G1 A%d\n", currTool*60); //rotate to current color slot
    fprintf(gcode, "G0 X0\n"); //rapid move to close to tool changer
    fprintf(gcode, "G1 X%f\n", toolChangePos); //slow move to dropoff
    fprintf(gcode, "G1 X0\n"); //slow move away from dropoff
  }
  //TOOLCHANGE END
  totalDist = totalDist/1000; //conversion to meters
  fprintf(gcode,GFOOTER);
  fprintf(gcode, "( Total distance traveled = %f m, numReord = %i, numComp = %i, pointsCount = %i, pathCount = %i)\n", totalDist, numReord, numCompOut, pointCountOut, pathCountOut);
  printf("( Total distance traveled = %f m, numReord = %i, numComp = %i, pointsCount = %i, pathCount = %i)\n", totalDist, numReord, numCompOut, pointCountOut, pathCountOut);
  printf("( size X%.4f Y%.4f x X%.4f Y%.4f )\n",minx,miny,maxx,maxy);
  fclose(gcode);
  free(points);
  free(paths);
  free(cities);
  nsvgDelete(g_image);
  free(penList);
  //send a signal to qml that the gcode is done
  


  // printf("writeCond reached = %d\n", writeCond);
  // printf("skipCond reached = %d\n", skipCond);
  return 0;
}

#define BTSVG
#ifndef BTSVG
int main(int argc, char* argv[]){
  printf("Argc:%d\n", argc);
  int penColorCount[6] = {2, 1, 1, 1, 0, 0}; //count of colors per pen needs to be passed into generateGcode. penColorCount[i] corresponds to pen tool i-1.
  int penOneColorArr[] = {65280, 16711680}; //Integer values of colors for each pen. -1 in an arr is placeholder for no colors to this arr.
  int penTwoColorArr[] = {1710618};
  int penThreeColorArr[] = {2763519};
  int penFourColorArr[] = {-1};
  int penFiveColorArr[] = {-1};
  int penSixColorArr[] = {-1};

  int *penColors[6]; //Init arr of pointers for pen colors.
  int *penOneColors = (int*)malloc(sizeof(int)*penColorCount[0]); //Malloc number of colors per pen to each pointer
  int *penTwoColors = (int*)malloc(sizeof(int)*penColorCount[1]);
  int *penThreeColors = (int*)malloc(sizeof(int)*penColorCount[2]);
  int *penFourColors = (int*)malloc(sizeof(int)*penColorCount[3]);
  int *penFiveColors = (int*)malloc(sizeof(int)*penColorCount[4]);
  int *penSixColors = (int*)malloc(sizeof(int)*penColorCount[5]);
  memset(penOneColors, 0, sizeof(int)*penColorCount[0]);
  memset(penTwoColors, 0, sizeof(int)*penColorCount[1]);
  memset(penThreeColors, 0, sizeof(int)*penColorCount[2]);
  memset(penFourColors, 0, sizeof(int)*penColorCount[3]);
  memset(penFiveColors, 0, sizeof(int)*penColorCount[4]);
  memset(penSixColors, 0, sizeof(int)*penColorCount[5]);
  //assign colors to malloc'd mem
  for(int i = 0; i<numTools; i++){
    for(int j = 0; j < penColorCount[i]; j++){
      switch(i) {
        case 0:
          printf("Assigning penOneColorArr[%d] for tool %d\n", j, i);
          penOneColors[j] = penOneColorArr[j];
          break;
        case 1:
          printf("Assigning penTwoColorArr[%d] for tool %d\n", j, i);
          penTwoColors[j] = penTwoColorArr[j];
          break;
        case 2:
          printf("Assigning penThreeColorArr[%d] for tool %d\n", j, i);
          penThreeColors[j] = penThreeColorArr[j];
          break;
        case 3:
          printf("Assigning penFourColorArr[%d] for tool %d\n", j, i);
          penFourColors[j] = penFourColorArr[j];
          break;
        case 4:
          printf("Assigning penFiveColorArr[%d] for tool %d\n", j, i);
          penFiveColors[j] = penFiveColorArr[j];
          break;
        case 5:
          printf("Assigning penSixColorArr[%d] for tool %d\n", j, i);
          penSixColors[j] = penSixColorArr[j];
          break;
      }
    }
  }

  penColors[0] = penOneColors; //Set arr pointers to malloc'd pointers
  penColors[1] = penTwoColors;
  penColors[2] = penThreeColors;
  penColors[3] = penFourColors;
  penColors[4] = penFiveColors;
  penColors[5] = penSixColors;

  float paperDimensions[2] = {1.0, 1.0};
  int res = generateGcode(argc, argv, penColors, penColorCount, paperDimensions, 1, 1, 25.4, 50.8, -3);
  //Free malloc'd memory
  free(penOneColors);
  free(penTwoColors);
  free(penThreeColors);
  free(penFourColors);
  free(penFiveColors);
  free(penSixColors);
  return res;
}
#endif
