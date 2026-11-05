#include "global.h"
#include ASSET_HEADER(course_data.h)

/* Course structs are embedded but not laid out in ROM order, so index them
 * explicitly. Order matches the course_data segment (standard courses first,
 * then Battle and Ending). */
static CourseData* const sCourseData[] = {
    &aCourseMuteCity1,  &aCourseSilence1,    &aCourseSandOcean1,  &aCourseDevilsForest1,
    &aCourseBigBlue1,   &aCoursePortTown1,   &aCourseSectorAlpha, &aCourseRedCanyon1,
    &aCourseDevilsForest2, &aCourseMuteCity2, &aCourseBigBlue2,   &aCourseWhiteLand1,
    &aCourseFireField,  &aCourseSilence2,    &aCourseSectorBeta,  &aCourseRedCanyon2,
    &aCourseWhiteLand2, &aCourseMuteCity3,   &aCourseRainbowRoad, &aCourseDevilsForest3,
    &aCourseSpacePlant, &aCourseSandOcean2,  &aCoursePortTown2,   &aCourseBigHand,
    &aCourseBattle,     &aCourseEnding,
};

CourseData* PspCourseData_Get(s32 index) {
    if ((index < 0) || (index >= (s32) ARRAY_COUNTU(sCourseData))) {
        return NULL;
    }
    return sCourseData[index];
}
