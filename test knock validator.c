/**
 * Smart Sentry Door Security System
 * Unit Tests for Secret Knock Validation
 * 
 * Author: [Your Name]
 * Date: February 7, 2026
 */

#include "knock_validator.h"
#include <stdio.h>
#include <string.h>

/* Test framework macros */
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s\n", message); \
            return 0; \
        } \
    } while(0)

#define RUN_TEST(test_func) \
    do { \
        printf("Running %s... ", #test_func); \
        if (test_func()) { \
            printf("PASS\n"); \
            tests_passed++; \
        } else { \
            tests_failed++; \
        } \
        tests_total++; \
    } while(0)

/* Global test counters */
static int tests_total = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Test Cases */

/**
 * Test 1: Perfect pattern match
 */
int test_perfect_match(void) {
    uint32_t timestamps[] = {0, 500, 750, 1250};
    uint32_t pattern[] = {500, 250, 500};
    
    AuthState_t result = validate_secret_knock(timestamps, 4, pattern, 3);
    
    TEST_ASSERT(result == ACCESS_GRANTED, 
                "Perfect match should grant access");
    
    return 1;
}

/**
 * Test 2: 10% slower tempo (should still match)
 */
int test_slower_tempo(void) {
    uint32_t timestamps[] = {0, 550, 825, 1375};  // 10% slower
    uint32_t pattern[] = {500, 250, 500};
    
    AuthState_t result = validate_secret_knock(timestamps, 4, pattern, 3);
    
    TEST_ASSERT(result == ACCESS_GRANTED, 
                "10% slower tempo should grant access");
    
    return 1;
}

/**
 * Test 3: 15% faster tempo (at tolerance boundary)
 */
int test_faster_tempo(void) {
    uint32_t timestamps[] = {0, 425, 638, 1063};  // 15% faster
    uint32_t pattern[] = {500, 250, 500};
    
    AuthState_t result = validate_secret_knock(timestamps, 4, pattern, 3);
    
    TEST_ASSERT(result == ACCESS_GRANTED, 
                "15% faster tempo should grant access (at boundary)");
    
    return 1;
}

/**
 * Test 4: 20% variation (should fail - exceeds tolerance)
 */
int test_excessive_variation(void) {
    uint32_t timestamps[] = {0, 600, 900, 1500};  // 20% slower
    uint32_t pattern[] = {500, 250, 500};
    
    AuthState_t result = validate_secret_knock(timestamps, 4, pattern, 3);
    
    TEST_ASSERT(result == ACCESS_DENIED, 
                "20% variation should deny access (exceeds tolerance)");
    
    return 1;
}

/**
 * Test 5: Wrong pattern
 */
int test_wrong_pattern(void) {
    uint32_t timestamps[] = {0, 500, 1000, 1500};  // Equal intervals
    uint32_t pattern[] = {500, 250, 500};          // Different pattern
    
    AuthState_t result = validate_secret_knock(timestamps, 4, pattern, 3);
    
    TEST_ASSERT(result == ACCESS_DENIED, 
                "Wrong pattern should deny access");
    
    return 1;
}

/**
 * Test 6: Too few knocks
 */
int test_too_few_knocks(void) {
    uint32_t timestamps[] = {0, 500};
    uint32_t pattern[] = {500, 250, 500};
    
    AuthState_t result = validate_secret_knock(timestamps, 2, pattern, 3);
    
    TEST_ASSERT(result == ACCESS_DENIED, 
                "Too few knocks should deny access");
    
    return 1;
}

/**
 * Test 7: Too many knocks
 */
int test_too_many_knocks(void) {
    uint32_t timestamps[] = {0, 500, 750, 1250, 1500, 2000};
    uint32_t pattern[] = {500, 250, 500};
    
    AuthState_t result = validate_secret_knock(timestamps, 6, pattern, 3);
    
    TEST_ASSERT(result == ACCESS_DENIED, 
                "Too many knocks should deny access");
    
    return 1;
}

/**
 * Test 8: NULL pointer inputs
 */
int test_null_inputs(void) {
    uint32_t pattern[] = {500, 250, 500};
    
    AuthState_t result = validate_secret_knock(NULL, 4, pattern, 3);
    
    TEST_ASSERT(result == ACCESS_DENIED, 
                "NULL input should deny access");
    
    return 1;
}

/**
 * Test 9: "Shave and a Haircut" pattern
 */
int test_shave_and_haircut(void) {
    // Classic pattern: KNOCK KNOCK ... KNOCK-KNOCK ... KNOCK ... KNOCK KNOCK
    uint32_t timestamps[] = {0, 300, 1100, 1400, 2200, 2900, 3200};
    uint32_t pattern[] = {300, 800, 300, 800, 700, 300};
    
    AuthState_t result = validate_secret_knock(timestamps, 7, pattern, 6);
    
    TEST_ASSERT(result == ACCESS_GRANTED, 
                "Shave and a Haircut pattern should grant access");
    
    return 1;
}

/**
 * Test 10: Nonce validation - correct nonce
 */
int test_nonce_correct(void) {
    uint32_t timestamps[] = {0, 537, 787, 1287};  // First interval = 500 + 37
    uint32_t pattern[] = {500, 250, 500};
    uint8_t nonce = 37;
    
    AuthState_t result = validate_knock_with_nonce(timestamps, 4, pattern, 3, nonce);
    
    TEST_ASSERT(result == ACCESS_GRANTED, 
                "Correct nonce should grant access");
    
    return 1;
}

/**
 * Test 11: Nonce validation - incorrect nonce
 */
int test_nonce_incorrect(void) {
    uint32_t timestamps[] = {0, 537, 787, 1287};  // Nonce = 37
    uint32_t pattern[] = {500, 250, 500};
    uint8_t nonce = 50;  // Wrong nonce
    
    AuthState_t result = validate_knock_with_nonce(timestamps, 4, pattern, 3, nonce);
    
    TEST_ASSERT(result == ACCESS_DENIED, 
                "Incorrect nonce should deny access");
    
    return 1;
}

/**
 * Test 12: Spatial coherence - legitimate knock
 */
int test_spatial_coherence_legitimate(void) {
    // Simulated timestamps from point source at top-center
    // TL=1000, TR=1150, BL=5200, BR=5350 (microseconds)
    uint32_t timestamps[4] = {1000, 1150, 5200, 5350};
    
    float coherence = calculate_spatial_coherence(timestamps);
    
    TEST_ASSERT(coherence > 0.85f, 
                "Legitimate knock should have high coherence");
    
    return 1;
}

/**
 * Test 13: Spatial coherence - replay attack
 */
int test_spatial_coherence_replay(void) {
    // Simulated timestamps from shaker device (inconsistent TDOA)
    uint32_t timestamps[4] = {1000, 3200, 2100, 5800};
    
    float coherence = calculate_spatial_coherence(timestamps);
    
    TEST_ASSERT(coherence < 0.70f, 
                "Replay attack should have low coherence");
    
    return 1;
}

/**
 * Test 14: Knock location - top center (lock area)
 */
int test_location_top_center(void) {
    // Top sensors arrive simultaneously, bottom delayed
    uint32_t timestamps[4] = {1000, 1100, 6000, 6100};
    
    KnockLocation_t location = determine_knock_location(timestamps);
    
    TEST_ASSERT(location == LOCATION_TOP_CENTER, 
                "Should detect knock at top-center (lock area)");
    
    return 1;
}

/**
 * Test 15: Knock location - bottom (kick)
 */
int test_location_bottom(void) {
    // Bottom sensors arrive first
    uint32_t timestamps[4] = {5000, 5100, 1000, 1100};
    
    KnockLocation_t location = determine_knock_location(timestamps);
    
    TEST_ASSERT(location == LOCATION_BOTTOM, 
                "Should detect impact at bottom (kick)");
    
    return 1;
}

/**
 * Test 16: Knock location - distributed (shoulder)
 */
int test_location_distributed(void) {
    // All sensors within narrow time window
    uint32_t timestamps[4] = {1000, 1500, 1800, 2200};
    
    KnockLocation_t location = determine_knock_location(timestamps);
    
    TEST_ASSERT(location == LOCATION_DISTRIBUTED, 
                "Should detect distributed impact (shoulder/body weight)");
    
    return 1;
}

/**
 * Test 17: Complex pattern with variable intervals
 */
int test_complex_pattern(void) {
    uint32_t timestamps[] = {0, 200, 600, 800, 1400, 2000};
    uint32_t pattern[] = {200, 400, 200, 600, 600};
    
    AuthState_t result = validate_secret_knock(timestamps, 6, pattern, 5);
    
    TEST_ASSERT(result == ACCESS_GRANTED, 
                "Complex pattern should be validated correctly");
    
    return 1;
}

/**
 * Test 18: Minimum valid pattern (2 knocks)
 */
int test_minimum_pattern(void) {
    uint32_t timestamps[] = {0, 500};
    uint32_t pattern[] = {500};
    
    AuthState_t result = validate_secret_knock(timestamps, 2, pattern, 1);
    
    TEST_ASSERT(result == ACCESS_GRANTED, 
                "Minimum pattern (2 knocks) should be valid");
    
    return 1;
}

/**
 * Test 19: Edge case - very fast knocking
 */
int test_very_fast_knocking(void) {
    uint32_t timestamps[] = {0, 100, 150, 250};  // Very fast
    uint32_t pattern[] = {100, 50, 100};
    
    AuthState_t result = validate_secret_knock(timestamps, 4, pattern, 3);
    
    TEST_ASSERT(result == ACCESS_GRANTED, 
                "Very fast knocking should still match pattern");
    
    return 1;
}

/**
 * Test 20: Edge case - very slow knocking
 */
int test_very_slow_knocking(void) {
    uint32_t timestamps[] = {0, 2000, 3000, 5000};  // Very slow
    uint32_t pattern[] = {2000, 1000, 2000};
    
    AuthState_t result = validate_secret_knock(timestamps, 4, pattern, 3);
    
    TEST_ASSERT(result == ACCESS_GRANTED, 
                "Very slow knocking should still match pattern");
    
    return 1;
}

/* Main test runner */
int main(void) {
    printf("===========================================\n");
    printf("Smart Sentry - Knock Validator Unit Tests\n");
    printf("===========================================\n\n");
    
    /* Run all tests */
    RUN_TEST(test_perfect_match);
    RUN_TEST(test_slower_tempo);
    RUN_TEST(test_faster_tempo);
    RUN_TEST(test_excessive_variation);
    RUN_TEST(test_wrong_pattern);
    RUN_TEST(test_too_few_knocks);
    RUN_TEST(test_too_many_knocks);
    RUN_TEST(test_null_inputs);
    RUN_TEST(test_shave_and_haircut);
    RUN_TEST(test_nonce_correct);
    RUN_TEST(test_nonce_incorrect);
    RUN_TEST(test_spatial_coherence_legitimate);
    RUN_TEST(test_spatial_coherence_replay);
    RUN_TEST(test_location_top_center);
    RUN_TEST(test_location_bottom);
    RUN_TEST(test_location_distributed);
    RUN_TEST(test_complex_pattern);
    RUN_TEST(test_minimum_pattern);
    RUN_TEST(test_very_fast_knocking);
    RUN_TEST(test_very_slow_knocking);
    
    /* Print summary */
    printf("\n===========================================\n");
    printf("Test Results:\n");
    printf("  Total:  %d\n", tests_total);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("===========================================\n");
    
    if (tests_failed == 0) {
        printf("All tests PASSED! ✓\n");
        return 0;
    } else {
        printf("Some tests FAILED! ✗\n");
        return 1;
    }
}