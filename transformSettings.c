#include "transformSettings.h"
#include <math.h>

TransformSettings calculateScaleForContentsToDrawspace(TransformSettings settings, float* bounds, float pointsWidth, float pointsHeight) {
    // code specific for contentsToDrawspace
    printf("Forcing scale to drawSpace\n");
    //settings.scale = 1;
    
    printf("    Unscaled Points width:%f, Points height:%f\n", pointsWidth, pointsHeight);
    float pointsRatio = pointsWidth / pointsHeight;
    float drawSpaceRatio = settings.drawSpaceWidth / settings.drawSpaceHeight;
    settings.scale = (drawSpaceRatio > pointsRatio) ? (settings.drawSpaceHeight / pointsHeight) : (settings.drawSpaceWidth / pointsWidth);
    return settings;
}

TransformSettings calculateScaleForFitToMaterial(TransformSettings settings) {
    // code specific for fitToMaterial
    printf("FitToMat\n");
    float materialRatio = settings.drawSpaceWidth / settings.drawSpaceHeight;
    float svgRatio = settings.loadedFileWidth / settings.loadedFileHeight;
    settings.scale = (materialRatio > svgRatio) ? (settings.drawSpaceHeight / settings.loadedFileHeight) : (settings.drawSpaceWidth / settings.loadedFileWidth);
    return settings;
}

TransformSettings calculateScaleForNotFitToMaterial(TransformSettings settings) {
    // code specific for !fitToMaterial
    printf("Not FitToMat\n");
    settings.scale = 1;
    return settings;
}

TransformSettings calcShiftAndCenter(TransformSettings settings) {
    printf("loadedFileWidth: %f loadedFileHeight: %f\n", settings.loadedFileWidth, settings.loadedFileHeight);
    printf("scale: %f\n", settings.scale);
    printf("xMarginLeft: %f, yMarginTop: %f\n", settings.xMarginLeft, settings.xMarginRight);
    //Use shifts in centerX and centerY calculations, not originalCenterX/Y calculations.

    //Calculating TARGET centerpoint for final drawing.
    if(settings.centerOnMaterial){ //Center on material forces center of svg to center of draw space.
      printf("Settings.centerOnMaterial == 1\n");
      settings.centerX = settings.xMarginLeft + settings.drawSpaceWidth/2;
      settings.centerY = settings.yMarginTop + settings.drawSpaceHeight/2;
    } else { //If not centering the final point on material, centerpoint should be shift positions + 1/2 dimension.
             //Loaded file dimensions have already been scaled so this shoudl work for scaled coordinates.
      printf("Settings.centerOnMaterial == 0\n");
      settings.centerX = settings.xMarginLeft + (settings.loadedFileWidth/2 * settings.scale);
      settings.centerY = settings.yMarginTop + (settings.loadedFileHeight/2 * settings.scale);
    }
    
    //REDO BELOW
    //FINDING CENTERPOINT OF PARSED DOCUMENT.
    settings.originalCenterX = settings.loadedFileWidth/2;
    settings.originalCenterY = settings.loadedFileHeight/2;
    if(settings.swapDim){
      float tempCent = settings.originalCenterX;
      settings.originalCenterX = settings.originalCenterY;
      settings.originalCenterY = tempCent;
    }

    //REDO

    //original centerX and original centerY are the points at which the scaled and shifted values are centered around
    //before the rotation is applied. These may be the same as the final center points too (as in the case where center
    //on material is true). Need a separate calc for centerX and centerY for correct translation back?
    printf("originalCenterX:%f, originalCenterY:%f\n", settings.originalCenterX, settings.originalCenterY);
    printf("centerX:%f, centerY:%f\n", settings.centerX, settings.centerY);
    fflush(stdout);

    settings.cosRot = cos((90 * settings.svgRotation) * (M_PI / 180)); 
    settings.sinRot = sin((90 * settings.svgRotation) * (M_PI / 180));

    printf("CosRot: %f, SinRot: %f\n", settings.cosRot, settings.sinRot);

    return settings;
}

//All calculations for shift and rotation are handled here.
TransformSettings calcTransform(NSVGimage * g_image, float * paperDimensions, int * generationConfig, float bounds[4]){
  TransformSettings settings;

  //Width and height of the document in mm.
  settings.contentsToDrawspace = generationConfig[10];
  float width = paperDimensions[9];
  float height = paperDimensions[10];
  settings.paperWidth = paperDimensions[0];
  settings.paperHeight = paperDimensions[1];
  settings.pointsToDocumentScale = 1;

  settings.loadedFileWidth = width; //Width and height of svg from frontend.
  settings.loadedFileHeight = height;
  settings.svgRotation = generationConfig[2]; //Rotation amount /90
  settings.xMarginLeft = paperDimensions[2]; //Margins
  settings.yMarginTop = paperDimensions[3];
  settings.xMarginRight = paperDimensions[7];
  settings.yMarginBottom = paperDimensions[8];
  settings.drawSpaceWidth = paperDimensions[0] - settings.xMarginLeft - settings.xMarginRight; //Amount of space for drawing.
  settings.drawSpaceHeight = paperDimensions[1] - settings.yMarginTop - settings.yMarginBottom;
  settings.pointBounds = bounds; //[xmin, ymin, xmax, ymax] bounding box of points in doc.
  //bounds of points
  
  printf("Viewbox info from g_image: viewMinx: %f, viewMiny: %f, viewWidth: %f, viewHeight: %f, alignType: %d\n", g_image->viewMinx, g_image->viewMiny, g_image->viewWidth, g_image->viewHeight, g_image->alignType);
  printf("Image width:%f, Image Height:%f\n", settings.loadedFileWidth, settings.loadedFileHeight);
  printf("PaperDimensions X:%f, PaperDimension Y:%f\n",paperDimensions[0], paperDimensions[1]);
  printf("drawSpaceWidth: %f, drawSpaceHeight:%f\n", settings.drawSpaceWidth, settings.drawSpaceHeight);

  settings.swapDim = ((generationConfig[2]%2) == 1);

  float pointsWidth = bounds[2] - bounds[0];
  float pointsHeight = bounds[3] - bounds[1];
  if (settings.contentsToDrawspace){
    settings.loadedFileWidth = pointsWidth;
    settings.loadedFileHeight = pointsHeight;
  }

  // Swap width and height if necessary. Need to calc this for contentsToDrawspace outside of the scale calc function.
  if (settings.swapDim) {
    float temp = settings.loadedFileWidth;
    settings.loadedFileWidth = settings.loadedFileHeight;
    settings.loadedFileHeight = temp;
    printf("Swapped image width:%f Image Height:%f\n", settings.loadedFileWidth, settings.loadedFileHeight);
  }

  //assume g_image->alignType = 0 for now. Currently inset values are not scaled to paper dimensions.
  settings.xInsetLeft = bounds[0] - g_image->viewMinx; // Left side of point bounding box distance from viewbox
  settings.xInsetRight = (g_image->width) - bounds[2]; // Right side
  settings.yInsetTop = bounds[1] - g_image->viewMiny; // Top side
  settings.yInsetBottom = (g_image->height) - bounds[3]; // Bottom side

  for (int i = 0; i < settings.svgRotation; i++) {
    float tempInset = settings.xInsetLeft;
    settings.xInsetLeft = settings.yInsetBottom;
    settings.yInsetBottom = settings.xInsetRight;
    settings.xInsetRight = settings.yInsetTop;
    settings.yInsetTop = tempInset;
  }

  // Determine if fitting to material is necessary
  // If file is too large, or we selected to fitToMaterial.
  settings.fitToMaterial = ((settings.loadedFileWidth > settings.drawSpaceWidth) || (settings.loadedFileHeight > settings.drawSpaceHeight) || generationConfig[0]); 
  printf("Fit to material: %d\n", settings.fitToMaterial);
  settings.centerOnMaterial = generationConfig[1];
  //SCALE CALCULATIONS

  if (settings.contentsToDrawspace) {
    printf("CALC CONTENTS TO DRAWSPACE\n");
    settings = calculateScaleForContentsToDrawspace(settings, bounds, settings.loadedFileWidth, settings.loadedFileHeight);
  } else if (settings.fitToMaterial) { //If fit to material is toggled or SVG is larger than draw space.
    printf("CALC FIT TO MAT\n");
    settings = calculateScaleForFitToMaterial(settings);
  } else {
    settings = calculateScaleForNotFitToMaterial(settings);
  }

  //END SCALE CALCULATIONS
  printf("    Scale%f\n", settings.scale);
  settings = calcShiftAndCenter(settings);
  
  return settings;
}

void printTransformSettings(TransformSettings settings) {
  printf("Point Bounds\n");
//   for(int i = 0; i < 4; i++){
//     printf("    Bounds[%d] = %f\n", i, bounds[i]);
//   }
  printf("\nscale: %f\n", settings.scale);
  printf("pointsToDocumentScale: %f\n", settings.pointsToDocumentScale);
  printf("drawingWidth: %f\n", settings.loadedFileWidth);
  printf("drawingHeight: %f\n", settings.loadedFileHeight);
  printf("drawSpaceWidth: %f\n", settings.drawSpaceWidth);
  printf("drawSpaceHeight: %f\n", settings.drawSpaceHeight);
  printf("loadedFileWidth: %f\n", settings.loadedFileWidth);
  printf("loadedFileHeight: %f\n", settings.loadedFileHeight);
  printf("shiftX: %f\n", settings.shiftX);
  printf("shiftY: %f\n", settings.shiftY);
  printf("centerX: %f\n", settings.centerX);
  printf("centerY: %f\n", settings.centerY);
  printf("originalCenterX: %f\n", settings.originalCenterX);
  printf("originalCenterY: %f\n", settings.originalCenterY);
  printf("cosRot: %f\n", settings.cosRot);
  printf("sinRot: %f\n", settings.sinRot);
  printf("xmarginleft: %f\n", settings.xMarginLeft);
  printf("xmarginright: %f\n", settings.xMarginRight);
  printf("ymargintop: %f\n", settings.yMarginTop);
  printf("ymarginbottom: %f\n", settings.yMarginBottom);
  printf("xInsetLeft: %f\n", settings.xInsetLeft);
  printf("xInsetRight: %f\n", settings.xInsetRight);
  printf("yInsetTop: %f\n", settings.yInsetTop);
  printf("yInsetBottom: %f\n", settings.yInsetBottom);
  printf("fitToMaterial: %d\n", settings.fitToMaterial);
  printf("centerOnMaterial: %d\n", settings.centerOnMaterial);
  printf("swapDim: %d\n", settings.swapDim);
  printf("svgRotation: %d\n", settings.svgRotation);
  printf("contentsToDrawspace: %d\n\n", settings.contentsToDrawspace);
}