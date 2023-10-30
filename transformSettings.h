#ifndef TRANSFORM_SETTINGS
#define TRANSFORM_SETTINGS

#include "stdio.h"

#ifndef NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#endif

typedef struct TransformSettings {
    float scale;
    float pointsToDocumentScale;
    float drawingWidth;
    float drawingHeight;
    float drawSpaceWidth;
    float drawSpaceHeight;
    float loadedFileWidth;
    float loadedFileHeight;
    float xInsetLeft;
    float yInsetTop;
    float xInsetRight;
    float yInsetBottom;
    float shiftX;
    float shiftY;
    float centerX;
    float centerY;
    float originalCenterX;
    float originalCenterY;
    float cosRot;
    float sinRot;
    float xMarginLeft;
    float xMarginRight;
    float yMarginTop;
    float yMarginBottom;
    int fitToMaterial;
    int centerOnMaterial;
    int swapDim;
    int svgRotation;
    int contentsToDrawspace;
    float* pointBounds;
    float paperWidth;
    float paperHeight;
} TransformSettings;

TransformSettings calculateScaleForContentsToDrawspace(TransformSettings settings, float* bounds, float pointsWidth, float pointsHeight);
TransformSettings calculateScaleForFitToMaterial(TransformSettings settings);
TransformSettings calculateScaleForNotFitToMaterial(TransformSettings settings);
TransformSettings calcShiftAndCenter(TransformSettings settings);
TransformSettings calcTransform(NSVGimage * g_image, float * paperDimensions, int * generationConfig, float bounds[4]);
void printTransformSettings(TransformSettings settings);


#endif // TRANSFORM_SETTINGS_H