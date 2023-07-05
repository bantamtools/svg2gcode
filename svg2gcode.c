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
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include "svg2gcode.h"
#include <math.h>

//#define DEBUG_OUTPUT
#define BTSVG
#define maxBez 128 //64;

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define GHEADER "G90\nG0 M3 S%d\n" //G92 X0 Y0\n //add here your specific G-codes
#define GHEADER_NEW "nG90\n" //G92 X0 Y0\n //add here your specific G-codes
#define CUTTERON "G0 M3 S%d\n"
#define CUTTEROFF "M5\n" // same for this
#define GFOOTER "M5\nM30\n "
#define GMODE "M4\n"

static int sixColorWidth = 306;

static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }
static int numTools = 6;
static float bounds[4];
static int pathCount,pointsCount,shapeCount;
static struct NSVGimage* g_image = NULL;
int numCompOut = 0;
int pathCountOut = 0;
int pointCountOut = 0;

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
  int id;
  NSVGpaint stroke;
} City;

SVGPoint bezPoints[maxBez];
static SVGPoint first,last;
static int bezCount = 0;
int collinear = 0;
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

float interpFeedrate(int A, int B, float m){ //fr X and fr Y
  if(m == -1){
    return B;
  }
  float scale_factor = (B-A) / (M_PI / 2.0);
  return A + scale_factor * atan(m);
}

float absoluteSlope(float x1, float y1, float x2, float y2){
  if(x2 - x1 == 0){
    return - 1;
  } else {
    return fabs((float)(y2-y1) / (x2 - x1));
  }
}

// bezier smoothing
static void cubicBez(float x1, float y1, float x2, float y2,
             float x3, float y3, float x4, float y4,
             float tol, int level)
{
  float x12,y12,x23,y23,x34,y34,x123,y123,x234,y234,x1234,y1234;
  float d;

  if (level > 12) {
    printf("cubicBez > lvl 12");
    return;
  }
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

  float crossProduct1 = (x2 - x1) * (y3 - y1) - (y2 - y1) * (x3 - x1);
  float crossProduct2 = (x2 - x1) * (y4 - y1) - (y2 - y1) * (x4 - x1);
  if (fabs(crossProduct1) == 0 && fabs(crossProduct2) == 0) {
    // The curve is a straight line
    collinear = 1;
  }

  d = distPtSeg(x1234, y1234, x1,y1, x4,y4);
  if (d > tol*tol) {
    cubicBez(x1,y1, x12,y12, x123,y123, x1234,y1234, tol, level+1);
    cubicBez(x1234,y1234, x234,y234, x34,y34, x4,y4, tol, level+1);
  } else {
    bezPoints[bezCount].x = x4; //number of points in a given curve will be bezCount.
    bezPoints[bezCount].y = y4;
    bezCount++;
    if(bezCount >= maxBez) {
      printf("!bez count\n");
      bezCount = maxBez;
    }
  }
}

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

static void calcPaths(SVGPoint* points, ToolPath* paths, int* npaths, City* cities, FILE* debug) {
  struct NSVGshape* shape;
  struct NSVGpath* path;
  int i, j, k, l, p, b, bezCount;
  SVGPoint* pts;
  bezCount = 0;
  i = 0;
  k = 0;
  j = 0;
  p = 0;
  int shapeCount = 0;

#ifdef DEBUG_OUTPUT
  fprintf(debug, "Debug Information:\n");
#endif

  for (shape = g_image->shapes; shape != NULL; shape = shape->next) {
#ifdef DEBUG_OUTPUT
    fprintf(debug, "Shape num :%d\n", shapeCount);
#endif
    for (path = shape->paths; path != NULL; path = path->next) {
      cities[i].id = i;
      cities[i].stroke = shape->stroke;
#ifdef DEBUG_OUTPUT
      fprintf(debug, "  City number %d, Npts:%d ,color = %d\n", i, path->npts, shape->stroke.color);
#endif
      for (j = 0; j < path->npts - 1; j += 3) {
        float* pp = &path->pts[j * 2];
        if (j == 0) {
            points[i].x = pp[0];
            points[i].y = pp[1];
        }
        bezCount++;
        for (b = 0; b < 8; b++) {
          paths[k].points[b] = pp[b];
        }
        paths[k].closed = path->closed;
        paths[k].city = i;
        k++;
      }
      cont:
      if (k > pointsCount) {
        printf("Error: k > pointsCount\n");
        *npaths = 0;
        return;
      }
      if (i > pathCount) {
        printf("Error: i > pathCount\n");
        exit(-1);
      }
      i++;
    }
    j++;
    shapeCount++;
  }
#ifdef DEBUG_OUTPUT
  fprintf(debug, "Total paths: %d, total points: %d\n", i, k);
#endif
  *npaths = k;
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
      colorMatch = 0;
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
static void reorder(SVGPoint* pts, int pathCount, char xy, City* cities, Pen* penList, int quality) {
  int i,j,k,temp1,temp2,indexA,indexB, indexH, indexL;
  City temp;
  float dx,dy,dist,dist2, dnx, dny, ndist, ndist2;
  SVGPoint p1,p2,p3,p4;
  SVGPoint pn1,pn2,pn3,pn4;
  int numComp = floor(sqrt(pointsCount) * (quality+1));
  for(i=0;i<numComp*pathCount;i++) {
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
  printf("\t-w final width in mm\n");
  printf("\t-t Bezier tolerance (0.5)\n");
  printf("\t-Z z-engage (-1.0)\n");
  printf("\t-B do Bezier curve smoothing\n");
  printf("\t-h this help\n");
  }

//want to rewrite the definition to contain integer values in one array, and float values in another so I don't have to keep passing more and more arguments.
//machineType 0 = 6-Color, 1 = LFP, 2 = MVP.
//create int config[], with [scaleToMaterial, centerSvg, svgRotation (rotate = 0,1,2,3) * 90, machineType] 

int generateGcode(int argc, char* argv[], int** penColors, int penColorCount[6], float paperDimensions[6], int generationConfig[9]) {
  printf("In Generate GCode\n");
  int i,j,k,l,first = 1;
  struct NSVGshape *shape1,*shape2;
  struct NSVGpath *path1,*path2;
  SVGPoint* points;
  ToolPath* toolPaths;
  City *cities; //Corresponds to an NSVGPath
  //all 6 tools will have their color assigned manually. If a path has a color not set in p1-6, assign to p1 by default.
  Pen *penList; //counts each color occurrence + the int assigned. (currently, assign any unknown/unsupported to p1. sum of set of pX should == nPaths;)
  //int numTools = 6;
  int npaths;
  int quality = generationConfig[8]; //0, 1, 2, low, med, high.

  int feed = generationConfig[5]; //need to add some logic on a per move basis on line slope to interp between x and y specific feedrates.
  int feedY = generationConfig[6];
  int zFeed = generationConfig[7];
  int tempFeed = 0;
  int slowTravel = 3500;
  int cityStart=1;
  float zFloor = paperDimensions[4];
  float ztraverse = paperDimensions[5]; //paperDimensions[5]; CALLED PENLIFT IN OSETTINGS AND FRONTED CODE
  float width = -1;
  float height =-1;
  char xy = 1;
  float w,h,widthInmm,heightInmm = -1.;
  //float scale = 1; //make this dynamic. //this changes with widthInmm
  float xmargin = paperDimensions[2]; //xmargin around drawn elements in mm
  float ymargin = paperDimensions[3]; //ymargin around drawn elements in mm
  //config initialization
  int fitToMaterial = generationConfig[0]; //scaleToMaterial
  int centerOnMaterial = generationConfig[1];//centerSvg;
  int svgRotation = generationConfig[2]; //svgRotation * 90 is current degrees of rotation.
  int machineType = generationConfig[3]; //machineType
  float centerX = 0;
  float centerY = 0;
  float originalCenterX = 0;
  float originalCenterY = 0;

  int currColor = 1; //if currColor == 1, then no tool is currently being held.
  int targetColor = 0;
  int targetTool = 0; //start as 0 so no tool is matched
  int currTool = -1; //-1 indicates no tool picked up
  int colorMatch = 0;
  float toolChangePos = -51.5;
  float tol; //In mm. Tolerance of x mm.
  int numReord; //Tol and numReord configured based on quality later in method.
  float x,y,bx,by,bxold,byold,firstx,firsty;
  double d;
  float xold,yold = 0;

  float maxx = -10000.,minx=10000.,maxy = -10000.,miny=10000.,zmax = -1000.,zmin = 1000;
  float shiftX = 0;
  float shiftY = 0;
  float yMountOffset = 0; //mm
  FILE *gcode;
  FILE *debug;
  int pwr = 90;
  int ch;
  int dwell = -1;
  char gbuff[128];

  printf("Argc:%d\n", argc);

  if(argc < 3) {
    help();
    return -1;
  }
  while((ch=getopt(argc,argv,"D:ABhf:n:s:Fz:Z:S:w:t:m:cTV1aLP:CY:X:")) != EOF) {
    switch(ch) {
    case 'P': pwr = atoi(optarg);
      break;
    case 'Y': shiftY = atof(optarg); // shift
      break;
    case 'X': shiftX = atof(optarg); // shift
      break;
    case 'h': help();
      break;
    case 'f': feed = atoi(optarg);
      break;
    case 'n': numReord = atoi(optarg);
      break;
    case 't': tol = atof(optarg);
      break;
    case 'w': widthInmm = atof(optarg);
      break;
    default: help();
      return(1);
      break;
    }
  }

  printf("File open string: %s\n", argv[optind]);
  printf("File output string: %s\n", argv[optind+1]);
  g_image = nsvgParseFromFile(argv[optind],"px",96);
  if(g_image == NULL) {
    printf("error: Can't open input %s\n",argv[optind]);
    return -1;
  }

  if(quality == 2){
    tol = 0.25;
    numReord = 20;
  } else if (quality == 1){
    tol = 0.5;
    numReord = 10;
  } else {
    tol = 1;
    numReord = 10;
  }

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

  printf("Getting rotation information\n");
  fflush(stdout);
  width = g_image->width;
  height = g_image->height;
  printf("Image width:%f Image Height:%f\n", width, height);

  //scaling + fitting operations.
  float drawSpaceWidth = paperDimensions[0] - (2*xmargin);
  float drawSpaceHeight = paperDimensions[1] - (2*ymargin);
  printf("drawSpaceWidth: %f, drawSpaceHeight:%f\n", drawSpaceWidth, drawSpaceHeight);
  float scale, drawingWidth, drawingHeight;
  int swap_dim = (svgRotation == 1 || svgRotation == 3);

  // Swap width and height if necessary
  if (swap_dim) {
      float temp = width;
      width = height;
      height = temp;
      printf("Swapped image width:%f Image Height:%f\n", width, height);
  }
  drawingWidth = width;
  drawingHeight = height;
  printf("DrawingWidth:%f, DrawingHeight:%f\n", drawingWidth, drawingHeight);
  fflush(stdout);

  // Determine if fitting to material is necessary
  fitToMaterial = ((drawingWidth > drawSpaceHeight) || (drawingHeight > drawSpaceHeight) || fitToMaterial);

  // If fitting to material, calculate scale and new drawing dimensions
  if (fitToMaterial) {
      float materialRatio = drawSpaceWidth / drawSpaceHeight;
      float svgRatio = width / height;
      scale = (materialRatio > svgRatio) ? (drawSpaceHeight / height) : (drawSpaceWidth / width);
      printf("Scale%f\n", scale);
      drawingWidth = width * scale;
      drawingHeight = height * scale;
      printf("Scaled drawingWidth:%f drawingHeight:%f\n", drawingWidth, drawingHeight);
      shiftX = xmargin;
      shiftY = ymargin;
  }

  // If centering on material, calculate shift
  if (centerOnMaterial) {
      shiftX = xmargin + ((drawSpaceWidth - drawingWidth) / 2);
      shiftY = ymargin + ((drawSpaceHeight - drawingHeight) / 2);
      printf("If centerOnMaterial shiftX:%f, shiftY:%f\n", shiftX, shiftY);
  }

  // Adjust for certain machine types
  if (machineType == 0) {
      shiftX += sixColorWidth - paperDimensions[0];
  }
  //Calculate center of un-scaled and un-rotated drawin


  // Calculate center of scaled and rotated drawing. 
  centerX = shiftX + drawingWidth / 2;
  centerY = shiftY + drawingHeight / 2;
  originalCenterX = centerX;
  originalCenterY = centerY;
  if(swap_dim){
    originalCenterX = shiftX + drawingHeight/2;
    originalCenterY = shiftY + drawingWidth/2;
  }

  printf("originalCenterX:%f, originalCenterY:%f\n", originalCenterX, originalCenterY);
  printf("centerX:%f, centerY:%f\n", centerX, centerY);
  fflush(stdout);

  float rotatedX, rotatedY, rotatedBX, rotatedBY, tempRot;
  float cosRot = cos((90*svgRotation)*(M_PI/180)); 
  float sinRot = sin((90*svgRotation)*(M_PI/180));

  printf("bounds %f %f X %f %f\n",bounds[0],bounds[1],bounds[2],bounds[3]);
  printf("SvgRotation:%i\n", svgRotation);
  printf("drawSpaceWidth:%f drawSpaceHeight:%f\n", drawSpaceWidth, drawSpaceHeight);
  printf("xMargin:%f yMargin:%f\n", xmargin, ymargin);
  printf("shiftX:%f, shiftY:%f\n", shiftX, shiftY);
  fflush(stdout);
  
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
  //fprintf(gcode, "( centerX:%f, centerY:%f )\n", centerX, centerY);
  // allocate memory
  points = (SVGPoint*)malloc(pathCount*sizeof(SVGPoint));
  toolPaths = (ToolPath*)malloc(pointsCount*sizeof(ToolPath));
  cities = (City*)malloc(pathCount*sizeof(City));
  memset(points, 0, pathCount*sizeof(SVGPoint));
  memset(toolPaths, 0, pointsCount*sizeof(ToolPath));
  memset(cities, 0, pathCount*sizeof(City));

  printf("Size of City: %lu, size of cities: %lu\n", sizeof(City), sizeof(City)*pathCount);
  npaths = 0;
#ifdef DEBUG_OUTPUT
  debug = fopen("debug.txt", "w");
  if(debug == NULL) {
    printf("Failed to open debug file\n");
    return -1;
  }
#endif
  calcPaths(points, toolPaths, &npaths, cities, debug);
#ifdef DEBUG_OUTPUT
  fclose(debug);
#endif
  printf("Reorder with numCities: %d\n",pathCount);
  for(k=0;k<numReord;k++) {
    reorder(points, pathCount, xy, cities, penList, quality);
    printf("%d... ",k);
    fflush(stdout);
  }
  printf("\n");

  //If cities are reordered by distances first, using a stable sort after for color should maintain the sort order obtained by distances, but organized by colors.
  printf("Sorting cities by color\n");
  int mergeCount = 0;
  int* mergeLevel = &mergeCount; 
  mergeSort(cities, 0, pathCount-1, 0, mergeLevel); //this is stable and can be called on subarrays. So we want to reorder, then call on subarrays indexed by our mapped colors.

  double totalDist = 0;

  printf("\n");
  if(first) {
    fprintf(gcode,GHEADER,pwr);
    fprintf(gcode, "G0 Z%f\n", ztraverse);
    if(machineType == 0 || machineType == 2) { //6Color or MVP
      fprintf(gcode, "G0 Z0\n");
      fprintf(gcode, "G1 Y0 F%i\n", feedY);
      fprintf(gcode, "G1 Y%f F%d\n", (-1.0*(paperDimensions[1]-100.0)), feedY);
      fprintf(gcode, "G1 Y0 F%i\n", feedY);
    }
    //fprintf(gcode,"G92X0Y0Z0\n");
  }

  k=0;
  i=0;

#ifdef DEBUG_OUTPUT
  FILE * printOut;
  printOut = fopen("writeDebug.txt", "w");
  if(printOut == NULL){
    printf("Failed to open printOut for debug\n");
    return -1;
  }
  fprintf(printOut, "PathCount:%d ShapeCount:%d PointCount:%d\n ", pathCount, shapeCount, pointsCount);
#endif

  for(i=0;i<pathCount;i++) { //equal to the number of cities, which is the number of NSVGPaths.
#ifdef DEBUG_OUTPUT
    fprintf(printOut, "City %d at i:%d\n", cities[i].id, i);
#endif
    cityStart=1;
    for(k=0;k<npaths;k++){ //npaths == number of points/ToolPaths in path. Looks at the city for each toolpath, and if it is equal to the city in this position's id
                            //in cities, then it beigs the print logic. This can almost certainly be optimized because each city does not have npaths paths associated.
      if(toolPaths[k].city == -1){ //means already written. Go back to start of above for loop and check next.
          continue;
      } else if(toolPaths[k].city == cities[i].id) {
#ifdef DEBUG_OUTPUT //print out toolpath info for chosen toolpath if debug.
        fprintf(printOut, "   ToolPath: %d, City: %d\n", k, cities[i].id);
        fprintf(printOut, "     Points:Cpx1:%f, Cpy1:%f, Cpx2:%f, Cpy2:%f, X1:%f, Y1:%f, X2:%f, Y2:%f\n", toolPaths[k].points[0], toolPaths[k].points[1], toolPaths[k].points[2], toolPaths[k].points[3], toolPaths[k].points[4], toolPaths[k].points[5], toolPaths[k].points[6], toolPaths[k].points[7]);
#endif
        break;
      }
    }
    if(k > npaths-1) {
      printf("Continue hit\n");
      continue;
    }

    //colorCheck and tracking for TOOLCHANGE
    if(cityStart == 1 && (machineType == 0 || machineType == 2)){ //City start and 6Color. MVP as well, wilk have different conditional for which tool to change to.
      targetColor = cities[i].stroke.color;
      if(targetColor != currColor) { //Detect tool slot of new color
        for(int p = 0; p < numTools; p++){
          if(colorInPen(penList[p], targetColor, penColorCount[p])){
            targetTool = p;
            break;
          }
          targetTool = 0;// if none of the tools matched this will always set target tool to default tool.
        }
      }
      if(targetTool != currTool){ //need to check if tool picked up previously or not
        if(machineType == 0){ //Maybe can abstract toolchanges to a different writeToolchange() method.
          if(currTool >= 0){ //tool is being held
            //fprintf(gcode, "( Tool change needed to tool %d )\n",targetTool+1);
            //add pickup and dropoff logic
            fprintf(gcode, "G1 A%d\n", currTool*60); //rotate to current color slot
            fprintf(gcode, "G1 Z%i F%i\n", 0, zFeed);
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
            fprintf(gcode, "G1 Z%i F%i\n", 0, zFeed);
            fprintf(gcode, "G0 X0\n"); //rapid move to close to tool changer
            fprintf(gcode, "G1 X%f F%d\n", toolChangePos ,slowTravel); //slow move to pickup
            fprintf(gcode, "G1 X0 F%d\n", slowTravel); //slow move away from pickup
            //fprintf(gcode, "( Tool change finished )\n");
          }
        } else if (machineType == 2 && (targetTool != 0)){
          fprintf(gcode, "( MVP PAUSE COMMAND TOOL:%d)\n", targetTool);
          //fprintf(gcode, "M0\n");
        }  
        currTool = targetTool;
      }
    }
    //TOOLCHANGE END

    //WRITING MOVES FOR DRAWING
    //First x and y point in a toolpath. Scale and shift.
    firstx = x = (toolPaths[k].points[0])*scale+shiftX;
    firsty = y =  (toolPaths[k].points[1])*scale+shiftY;

    //ROTATION CODE
    if(svgRotation > 0){
      //Apply transformation to center. CenterX and CenterY should carry scale and shift with them.
      rotatedX = (firstx - originalCenterX)*cosRot - (firsty - originalCenterY)*sinRot + centerX;
      rotatedY = (firstx - originalCenterX)*sinRot + (firsty - originalCenterY)*cosRot + centerY;
      firstx = x = rotatedX;
      firsty = y = rotatedY;
    }

    firsty = -firsty;
    y = -y;
    maxx = x;
    minx = x;
    maxy = y;
    miny = y;

    fprintf(gcode, "G1 Z%f F%d\n", ztraverse, zFeed);
    fprintf(gcode,"( city %d, color %d )\n", cities[i].id, cities[i].stroke.color);
    fprintf(gcode,"G0 X%.4f Y%.4f\n", x, y);
    fprintf(gcode, "G1 Z%f F%d\n", zFloor, zFeed);

    cityStart = 0;
    xold, bxold = x;
    yold, byold = y;
    for(j=k;j<npaths;j++) {
      // xold = x;
      // yold = y;
      first = 1;
      int level;
      if(toolPaths[j].city == cities[i].id) {
        //everything is a bezIer curve WOOOO. Each ToolPath has 8 points, as specified by nanoSvg.
        bezCount = 0;
        level = 0;
        collinear = 0;
        cubicBez(toolPaths[j].points[0], toolPaths[j].points[1], toolPaths[j].points[2], toolPaths[j].points[3], toolPaths[j].points[4], toolPaths[j].points[5], toolPaths[j].points[6], toolPaths[j].points[7], tol, level);
        // bxold=x;
        // byold=y;

        //Arc weld points in bezPoints here. Iterate through bezPoints with bezCount, weld as many points into arcs as possile. Arc weld on bezCount > 1?
        //fprintf(gcode, "( Toolpath:%d, collinear:%d, BezCount:%d\n )", j, collinear, bezCount);
        for(l = 0; l < bezCount; l++) {
          if(bezPoints[l].x > bounds[2] || bezPoints[l].x < bounds[0] || isnan(bezPoints[l].x)) {
            printf("bezPoints %f %f\n",bezPoints[l].x,bounds[0]);
            continue;
          }
          if(bezPoints[l].y > bounds[3]) {
            printf("bezPoints y %d\n",l);
            continue;
          }
          bx = (bezPoints[l].x)*scale+shiftX;
          by = (bezPoints[l].y)*scale+shiftY;

          //ROTATION FOR bx and by
          if(svgRotation > 0){
            //Apply transformation to center
            rotatedBX = (bx - originalCenterX)*cosRot - (by - originalCenterY)*sinRot + centerX;
            rotatedBY = (bx - originalCenterX)*sinRot + (by - originalCenterY)*cosRot + centerY;
            bx = rotatedBX;
            by = rotatedBY;
          }

          by = -by;
          maxx = bx;
          minx = bx;
          maxy = by;
          miny = by;

          d = distanceBetweenPoints(bxold, byold, bx, by);
          totalDist += d;

          tempFeed = interpFeedrate(feed, feedY, absoluteSlope(bxold, byold, bx, by));
          //fprintf(gcode, "( Line DEBUG x1:%f y1:%f x2:%f y2:%f )\n", bxold, byold, bx, by);
          fprintf(gcode,"G1 X%.4f Y%.4f  F%d\n",bx,by, tempFeed);

          bxold = bx;
          byold = by;
        } 
        toolPaths[j].city = -1; // This path has been written
      } else {
        break;
      }
    }
    if(toolPaths[j].closed) { //Line back to first point if path is closed.
      tempFeed = interpFeedrate(feed, feedY, absoluteSlope(bxold, byold, bx, by));
      fprintf(gcode,"G1 X%.4f Y%.4f  F%d\n", firstx, firsty, tempFeed);
      bxold = firstx;
      byold = firsty;
      xold = firstx;
      yold = firsty;
      fprintf(gcode, "G1 Z%f F%d\n", ztraverse, zFeed);
    }
    //END WRITING MOVES FOR DRAWING SECTION
  }
  if(machineType == 1 || machineType == 2){ //Lift to traverse height after job
    fprintf(gcode, "G1 Z%f F%i\n", ztraverse, zFeed);
  } else if (machineType == 0){ //Lift to zero for tool dropoff after job
    fprintf(gcode, "G1 Z%f F%i\n", 0, zFeed);
  }
  //drop off current tool
  if(machineType == 0){ //6Color
    fprintf(gcode, "G1 A%d\n", currTool*60); //rotate to current color slot
    fprintf(gcode, "G0 X0\n"); //rapid move to close to tool changer
    fprintf(gcode, "G1 X%f\n", toolChangePos); //slow move to dropoff
    fprintf(gcode, "G1 X0\n"); //slow move away from dropoff
  }

  totalDist = totalDist/1000; //conversion to meters
  //send paper to front
  fprintf(gcode, "G0 X0 Y0\n");
  fprintf(gcode,GFOOTER);
  fprintf(gcode, "( Total distance traveled = %f m, numReord = %i, numComp = %i, pointsCount = %i, pathCount = %i)\n", totalDist, numReord, numCompOut, pointCountOut, pathCountOut);
  printf("( Total distance traveled = %f m, numReord = %i, numComp = %i, pointsCount = %i, pathCount = %i)\n", totalDist, numReord, numCompOut, pointCountOut, pathCountOut);
  printf("( size X%.4f Y%.4f x X%.4f Y%.4f )\n",minx,miny,maxx,maxy);
  fclose(gcode);
  free(points);
  free(toolPaths);
  free(cities);
  free(penList);
  nsvgDelete(g_image);
  //send a signal to qml that the gcode is done
  
#ifdef DEBUG_OUTPUT
  fclose(printOut);
#endif
  // printf("writeCond reached = %d\n", writeCond);
  // printf("skipCond reached = %d\n", skipCond);
  return 0;
}

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
