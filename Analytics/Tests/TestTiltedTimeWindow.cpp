/** 
 * Copyright 2011 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */ 


#include "TestTiltedTimeWindow.h"

void TestTiltedTimeWindow::basic() {
    QMap<char, uint> granularitiesDefault;
    granularitiesDefault.insert('Q', 4);
    granularitiesDefault.insert('H', 24);
    granularitiesDefault.insert('D', 31);
    granularitiesDefault.insert('M', 12);
    granularitiesDefault.insert('Y', 1);
    TTWDefinition defaultTTWDefinition(900,
                                       granularitiesDefault,
                                       QList<char>() << 'Q' << 'H' << 'D' << 'M' << 'Y');

    Bucket U = TTW_BUCKET_UNUSED;
    TiltedTimeWindow * ttw = new TiltedTimeWindow();
    ttw->build(defaultTTWDefinition);


    QList<SupportCount> supportCounts;
    // First hour: first four quarters.
    supportCounts << 45 << 67 << 88 << 93;
    // Second hour.
    supportCounts << 34 << 49 << 36 << 97;
    // Third hour.
    supportCounts << 50 << 50 << 50 << 50;
    // Hours 4-23.
    for (int i = 3; i <= 23; i++)
        supportCounts << 25 << 25 << 25 << 25;
    // First quarter of second day to provide tipping point: now the 24
    // hour buckets are all filled.
    supportCounts << 10;
    // Four more quarters, meaning that the first hour of the second day
    // will be completed *and* another quarter is added, which will provide
    // the tipping point to fill the first day bucket.
    supportCounts << 10 << 10 << 10 << 20;
    // And finally, four more quarters, which will ensure there are 2 hours
    // of the second day.
    supportCounts << 20 << 20 << 20 << 30;

    // First hour.
    for (int i = 0; i < 4; i++)
        ttw->append(supportCounts[i], i+1);
    QCOMPARE(ttw->getBuckets(4), QVector<SupportCount>() << 93 << 88 << 67 << 45);
    QCOMPARE(ttw->getOldestBucketFilled(), (Bucket) 3);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 4);

    // Second hour.
    for (int i = 4; i < 8; i++)
        ttw->append(supportCounts[i], i+1);
    QCOMPARE(ttw->getBuckets(5), QVector<SupportCount>() <<  97 << 36 << 49 << 34
                                              << 293);
    QCOMPARE(ttw->getOldestBucketFilled(), (Bucket) 4);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 8);

    // Third hour.
    for (int i = 8; i < 12; i++)
        ttw->append(supportCounts[i], i+1);
    QCOMPARE(ttw->getBuckets(6), QVector<SupportCount>() <<  50 <<  50 <<  50 <<  50
                                              << 216 << 293);
    QCOMPARE(ttw->getOldestBucketFilled(), (Bucket) 5);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 12);

    // Hours 4-23.
    for (int i = 12; i < 96 ; i++)
        ttw->append(supportCounts[i], i+1);
    QCOMPARE(ttw->getBuckets(28), QVector<SupportCount>() <<  25 <<  25 <<  25 <<  25
                                              << 100 << 100 << 100
                                              << 100 << 100 << 100
                                              << 100 << 100 << 100
                                              << 100 << 100 << 100
                                              << 100 << 100 << 100
                                              << 100 << 100 << 100
                                              << 100 << 100 << 200
                                              << 216 << 293 <<   U);
    QCOMPARE(ttw->getOldestBucketFilled(), (Bucket) 26);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 96);

    // First quarter of second day to provide tipping point: now the 24
    // hour buckets are all filled.
    ttw->append(supportCounts[96], 97);
    QCOMPARE(ttw->getBuckets(28), QVector<SupportCount>() <<  10 <<   U <<   U <<   U
                                              << 100 << 100 << 100
                                              << 100 << 100 << 100
                                              << 100 << 100 << 100
                                              << 100 << 100 << 100
                                              << 100 << 100 << 100
                                              << 100 << 100 << 100
                                              << 100 << 100 << 100
                                              << 200 << 216 << 293);
    QCOMPARE(ttw->getOldestBucketFilled(), (Bucket) 27);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 97);

    // Four more quarters, meaning that the first hour of the second day
    // will be completed *and* another quarter is added, which will provide
    // the tipping point to fill the first day bucket.
    for (int i = 97; i < 101 ; i++)
        ttw->append(supportCounts[i], i+1);
    QCOMPARE(ttw->getBuckets(29), QVector<SupportCount>() << 20 <<   U <<   U <<   U
                                              <<  40 <<   U <<   U
                                              <<   U <<   U <<   U
                                              <<   U <<   U <<   U
                                              <<   U <<   U <<   U
                                              <<   U <<   U <<   U
                                              <<   U <<   U <<   U
                                              <<   U <<   U <<   U
                                              <<   U <<   U <<   U
                                              << 2809); // 2809 = 21*100 + 200 + 216 + 293
    QCOMPARE(ttw->getOldestBucketFilled(), (Bucket) 28);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 101);

    // Four more quarters, meaning that the second hour of the second day will
    // be completed. This is a test to check if the "oldestBucketFilled"
    // variable updates correctly: it should remain set to 28, and should not
    // be reset to 5. Since the second hour is added (which means the first
    // hour shifts from bucket 4 to bucket 5), this is a logic edge case that
    // may be expected.
    for (int i = 101; i < 105; i++)
        ttw->append(supportCounts[i], i+1);
    QCOMPARE(ttw->getOldestBucketFilled(), (Bucket) 28);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 105);

    // Drop tail starting at Granularity 1. This means only the value in the
    // first granularity (buckets 0, 1, 2 and 3) are kept, and all subsequent
    // granularities (and buckets) are reset.
    ttw->dropTail((Granularity) 1);
    QVector<SupportCount> buckets = ttw->getBuckets(defaultTTWDefinition.numBuckets);
    QCOMPARE(buckets[0], (SupportCount) 30);
    for (int g = 1; g < defaultTTWDefinition.numBuckets; g++)
        QCOMPARE(buckets[g], (SupportCount) -1);
    QCOMPARE(ttw->getOldestBucketFilled(), (Bucket) 3);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 105);

    // Append to the last quarter. This should not update the last update ID.
    ttw->append(100, 105);
    buckets = ttw->getBuckets(defaultTTWDefinition.numBuckets);
    QCOMPARE(buckets[0], (SupportCount) 130);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 105);

    delete ttw;
}

/**
 * The sliding window occurs for the last granularity in a TiltedTimeWindow: old
 * data is removed. Here, we test it for both a TTWDefinition with a single
 * granularity and one with two granularities.
 */
void TestTiltedTimeWindow::slidingWindow() {
    Bucket U = TTW_BUCKET_UNUSED;
    TiltedTimeWindow * ttw = new TiltedTimeWindow();
    QList<SupportCount> supportCounts;

    // TTWDefinition with single granularity.
    QMap<char, uint> granularitiesSingle;
    granularitiesSingle.insert('H', 4);
    TTWDefinition singleGranularityTTWDefinition(3600,
                                                 granularitiesSingle,
                                                 QList<char>() << 'H');

    ttw->build(singleGranularityTTWDefinition, true);

    supportCounts.clear();
    // Four hours of data.
    supportCounts << 1 << 2 << 3 << 4;
    // Fifth hour.
    supportCounts << 5;

    // First four hours.
    for (int i = 0; i < 4; i++)
        ttw->append(supportCounts[i], i+1);
    QCOMPARE(ttw->getBuckets(4), QVector<SupportCount>() << 4 << 3 << 2 << 1);
    QCOMPARE(ttw->getOldestBucketFilled(), (Bucket) 3);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 4);

    // Fifth hour.
    ttw->append(supportCounts[4], 5);
    QCOMPARE(ttw->getBuckets(4), QVector<SupportCount>() << 5 << 4 << 3 << 2);
    QCOMPARE(ttw->getOldestBucketFilled(), (Bucket) 3);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 5);




    // TTWDefinition with double granularity.
    QMap<char, uint> granularitiesDouble;
    granularitiesDouble.insert('Q', 4);
    granularitiesDouble.insert('H', 2);
    TTWDefinition doubleGranularityTTWDefinition(3600,
                                                 granularitiesDouble,
                                                 QList<char>() << 'Q' << 'H');

    ttw->build(doubleGranularityTTWDefinition, true);

    supportCounts.clear();
    // Four quarters of data: 1st hour.
    supportCounts << 10 << 10 << 10 << 10;
    // Four quarters of data: 2nd hour.
    supportCounts << 20 << 20 << 20 << 20;
    // Four quarters of data: 3rd hour.
    supportCounts << 30 << 30 << 30 << 30;
    // One quarter of data.
    supportCounts << 40;

    // First, second & third hour.
    for (int i = 0; i < 12; i++)
        ttw->append(supportCounts[i], i+1);
    QCOMPARE(ttw->getBuckets(6), QVector<SupportCount>() << 30 << 30 << 30 << 30 << 80 << 40);
    QCOMPARE(ttw->getOldestBucketFilled(), (Bucket) 5);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 12);

    // First quarter of fourth hour.
    ttw->append(supportCounts[12], 13);
    QCOMPARE(ttw->getBuckets(6), QVector<SupportCount>() << 40 << U << U << U << 120 << 80);
    QCOMPARE(ttw->getOldestBucketFilled(), (Bucket) 5);
    QCOMPARE(ttw->getLastUpdate(), (unsigned int) 13);
}
