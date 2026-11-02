/* unity.c - minimal Unity-style runner backing. */
#include "unity.h"

UnityState unity_state = {0, 0};

int UNITY_END(void)
{
    printf("\n%+d - %d Tests %d Failures %d Ignored\n",
           unity_state.tests_run - unity_state.tests_failed,
           unity_state.tests_run,
           unity_state.tests_failed,
           0);
    return (unity_state.tests_failed == 0) ? 0 : 1;
}
